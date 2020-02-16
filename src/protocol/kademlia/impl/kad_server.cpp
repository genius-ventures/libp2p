/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <libp2p/common/types.hpp>
#include <libp2p/crypto/sha/sha256.hpp>
#include <libp2p/network/connection_manager.hpp>
#include <libp2p/protocol/kademlia/impl/kad_server.hpp>

namespace libp2p::protocol::kademlia {

  std::array<KadServer::RequestHandler, Message::kTableSize>
      KadServer::request_handlers_table = {
          &KadServer::onPutValue,    &KadServer::onGetValue,
          &KadServer::onAddProvider, &KadServer::onGetProviders,
          &KadServer::onFindNode,    &KadServer::onPing};

  KadServer::KadServer(Host &host, KadImpl &kad)
      : host_(host),
        kad_(kad),
        protocol_(kad_.config().protocolId),
        log_("kad", "KadServer", &kad) {
    host_.setProtocolHandler(protocol_,
                             [wptr = weak_from_this()](
                                 protocol::BaseProtocol::StreamResult rstream) {
                               auto h = wptr.lock();
                               if (h) {
                                 h->handle(std::move(rstream));
                               }
                             });
    log_.info("started");
  }

  KadServer::~KadServer() = default;

  void KadServer::handle(StreamResult rstream) {
    if (!rstream) {
      return;
    }

    auto stream = rstream.value();

    connection::Stream *s = stream.get();
    assert(sessions_.find(s) == sessions_.end());
    auto session = std::make_shared<KadProtocolSession>(weak_from_this(),
                                                        std::move(stream));
    if (!session->read()) {
      s->reset();
      return;
    }
    session->state(reading_from_peer);
    sessions_[s] = std::move(session);
  }

  KadProtocolSession::Ptr KadServer::findSession(connection::Stream *from) {
    auto it = sessions_.find(from);
    if (it == sessions_.end()) {
      return nullptr;
    }
    return it->second;
  }

  void KadServer::onMessage(connection::Stream *from, Message &&msg) {
    auto session = findSession(from);
    if (!session)
      return;

    bool close_session = (msg.type >= Message::kTableSize)
        || (not(this->*(request_handlers_table[msg.type]))(msg))  // NOLINT
        || (not session->write(msg));

    if (close_session) {
      closeSession(from);
    } else {
      session->state(writing_to_peer);
    }
  }

  void KadServer::onCompleted(connection::Stream *from,
                              outcome::result<void> res) {
    auto session = findSession(from);
    if (!session)
      return;
    closeSession(from);
  }

  void KadServer::closeSession(connection::Stream *s) {
    auto it = sessions_.find(s);
    if (it != sessions_.end()) {
      it->second->close();
      sessions_.erase(s);
    }
  }

  bool KadServer::onPutValue(Message &msg) {
    if (!msg.record) {
      return false;
    }
    auto &r = msg.record.value();

    // TODO(artem): validate with external validator

    auto res = kad_.getLocalValueStore().putValue(r.key, r.value);
    if (!res) {
    }

    return false;  // no response
  }

  bool KadServer::onGetValue(Message &msg) {
    if (msg.key.empty()) {
      return false;
    }

    auto r = ContentAddress::fromWire(msg.key);
    if (!r) {
      // TODO(artem): log
      return false;
    }
    ContentAddress cid = std::move(r.value());

    PeerIdVec ids = kad_.getContentProvidersStore().getProvidersFor(cid);

    if (!ids.empty()) {
      // TODO(artem): refactor redundant boilerplate

      msg.provider_peers = Message::Peers{};
      auto &v = msg.provider_peers.value();

      size_t max_peers = kad_.config().closer_peers_count;

      v.reserve(max_peers);
      for (const auto &p : ids) {
        auto info = host_.getPeerRepository().getPeerInfo(p);
        if (info.addresses.empty()) {
          continue;
        }
        auto connectedness =
            host_.getNetwork().getConnectionManager().connectedness(info);
        v.push_back({std::move(info), connectedness});
        if (v.size() >= max_peers) {
          break;
        }
      }
    }

    LocalValueStore::AbsTime ts = 0;
    auto res = kad_.getLocalValueStore().getValue(cid, ts);
    if (res) {
      msg.record = Message::Record{std::move(cid), std::move(res.value()),
                                   std::to_string(ts)};
    }

    return true;
  }

  bool KadServer::onAddProvider(Message &msg) {
    // TODO(artem): validate against sender id

    if (!msg.provider_peers) {
      return false;
    }
    ContentAddress cid(msg.key);
    auto providers = msg.provider_peers.value();
    for (auto &p : providers) {
      kad_.getContentProvidersStore().addProvider(cid, p.info.id);
      kad_.addPeer(std::move(p.info), false);
    }

    return false;  // doesnt respond
  }

  bool KadServer::onGetProviders(Message &msg) {
    if (msg.key.empty()) {
      return false;
    }

    auto r = ContentAddress::fromWire(msg.key);
    if (!r) {
      // TODO(artem): log
      return false;
    }
    ContentAddress cid = std::move(r.value());

    PeerIdVec ids = kad_.getContentProvidersStore().getProvidersFor(cid);

    if (!ids.empty()) {
      // TODO(artem): refactor redundant boilerplate

      msg.provider_peers = Message::Peers{};
      auto &v = msg.provider_peers.value();

      size_t max_peers = kad_.config().closer_peers_count;

      v.reserve(max_peers);
      for (const auto &p : ids) {
        auto info = host_.getPeerRepository().getPeerInfo(p);
        if (info.addresses.empty()) {
          continue;
        }
        auto connectedness =
            host_.getNetwork().getConnectionManager().connectedness(info);
        v.push_back({std::move(info), connectedness});
        if (v.size() >= max_peers) {
          break;
        }
      }

      if (kad_.getLocalValueStore().has(cid)) {
        // this host also provides
        v.push_back({host_.getPeerInfo(), Message::Connectedness::CONNECTED});
      }
    }

    return true;
  }

  bool KadServer::onFindNode(Message &msg) {
    if (msg.closer_peers) {
      for (auto &p : msg.closer_peers.value()) {
        if (p.conn_status == Message::Connectedness::CAN_CONNECT) {
          kad_.addPeer(std::move(p.info), false);
        }
      }
    }
    msg.closer_peers.reset();

    auto id_res =
        peer::PeerId::fromBytes(gsl::span(msg.key.data(), msg.key.size()));
    if (id_res) {
      size_t max_peers = kad_.config().closer_peers_count;

      PeerIdVec ids = kad_.getNearestPeers(NodeId(id_res.value()));

      msg.closer_peers = Message::Peers{};
      auto &v = msg.closer_peers.value();

      v.reserve(max_peers);
      for (const auto &p : ids) {
        auto info = host_.getPeerRepository().getPeerInfo(p);
        if (info.addresses.empty()) {
          continue;
        }
        auto connectedness =
            host_.getNetwork().getConnectionManager().connectedness(info);
        v.push_back({std::move(info), connectedness});
        if (v.size() >= max_peers) {
          break;
        }
      }
    }

    return true;
  }

  bool KadServer::onPing(Message &msg) {
    if (msg.closer_peers) {
      for (auto &p : msg.closer_peers.value()) {
        if (p.conn_status == Message::Connectedness::CAN_CONNECT) {
          kad_.addPeer(std::move(p.info), false);
        }
      }
    }

    msg.clear();
    return true;
  }

}  // namespace libp2p::protocol::kademlia
