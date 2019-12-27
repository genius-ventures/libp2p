/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <libp2p/protocol/gossip/impl/remote_subscriptions.hpp>

#include <algorithm>

#include <libp2p/protocol/gossip/impl/connectivity.hpp>
#include <libp2p/protocol/gossip/impl/message_builder.hpp>

namespace libp2p::protocol::gossip {

  RemoteSubscriptions::RemoteSubscriptions(const Config &config,
                                           Connectivity &connectivity,
                                           Scheduler &scheduler)
      : config_(config), connectivity_(connectivity), scheduler_(scheduler) {}

  void RemoteSubscriptions::onSelfSubscribed(bool subscribed,
                                             const TopicId &topic) {
    auto res = getItem(topic, subscribed);
    if (!res) {
      // TODO(artem): log error
      return;
    }
    TopicSubscriptions &subs = res.value();
    subs.onSelfSubscribed(subscribed);
    if (subs.empty()) {
      table_.erase(topic);
    }
  }

  void RemoteSubscriptions::onPeerSubscribed(const PeerContextPtr &peer,
                                             bool subscribed,
                                             const TopicId &topic) {
    auto res = getItem(topic, subscribed);
    if (!res) {
      // not error in this case, this is request from wire...
      return;
    }
    TopicSubscriptions &subs = res.value();

    if (subscribed) {
      subs.onPeerSubscribed(peer);
    } else {
      subs.onPeerUnsubscribed(peer);
      if (subs.empty()) {
        table_.erase(topic);
      }
    }
  }

  void RemoteSubscriptions::onPeerDisconnected(const PeerContextPtr &peer) {
    for (const auto &topic : peer->subscribed_to) {
      onPeerSubscribed(peer, false, topic);
    }
  }

  bool RemoteSubscriptions::hasTopic(const TopicId &topic) {
    return table_.count(topic) != 0;
  }

  bool RemoteSubscriptions::hasTopics(const TopicList &topics) {
    for (const auto &topic : topics) {
      if (hasTopic(topic)) {
        return true;
      }
    }
    return false;
  }

  void RemoteSubscriptions::onGraft(const PeerContextPtr &peer,
                                    const TopicId &topic) {
    auto res = getItem(topic, false);
    if (!res) {
      // we don't have this topic anymore
      peer->message_to_send->addPrune(topic);
      connectivity_.peerIsWritable(peer, true);
      return;
    }
    res.value().onGraft(peer);
  }

  void RemoteSubscriptions::onPrune(const PeerContextPtr &peer,
                                    const TopicId &topic) {
    auto res = getItem(topic, false);
    if (!res) {
      return;
    }
    res.value().onPrune(peer);
  }

  void RemoteSubscriptions::forwardMessage(const TopicMessage::Ptr &msg,
                                           const MessageId &msg_id,
                                           bool is_published_locally) {
    auto now = scheduler_.now();
    for (const auto &topic : msg->topic_ids) {
      auto res = getItem(topic, is_published_locally);
      if (!res) {
        // TODO(artem): log it. if (is_published_locally) then this is error
        continue;
      }
      res.value().onNewMessage(msg, msg_id, is_published_locally, now);
    }
  }

  void RemoteSubscriptions::onHeartbeat() {
    auto now = scheduler_.now();
    for (auto it = table_.begin(); it != table_.end();) {
      it->second.onHeartbeat(now);
      if (it->second.empty()) {
        // fanout interval expired - clean up
        it = table_.erase(it);
      } else {
        ++it;
      }
    }
  }

  boost::optional<RemoteSubscriptions::TopicSubscriptions &>
  RemoteSubscriptions::getItem(const TopicId &topic, bool create_if_not_exist) {
    auto it = table_.find(topic);
    if (it != table_.end()) {
      return it->second;
    }
    if (create_if_not_exist) {
      auto [it, _] = table_.emplace(
          topic, TopicSubscriptions(topic, config_, connectivity_));
      return it->second;
    }
    return boost::none;
  }

  RemoteSubscriptions::TopicSubscriptions::TopicSubscriptions(
      TopicId topic, const Config &config, Connectivity &connectivity)
      : topic_(std::move(topic)),
        config_(config),
        connectivity_(connectivity),
        self_subscribed_(false),
        fanout_period_ends_(0) {}

  bool RemoteSubscriptions::TopicSubscriptions::empty() {
    return (not self_subscribed_) && (fanout_period_ends_ == 0)
        && subscribed_peers_.empty() && mesh_peers_.empty();
  }

  void RemoteSubscriptions::TopicSubscriptions::onNewMessage(
      const TopicMessage::Ptr &msg, const MessageId &msg_id,
      bool is_published_locally, Time now) {
    if (is_published_locally) {
      fanout_period_ends_ = now + config_.seen_cache_lifetime_msec;
    }

    mesh_peers_.selectAll([this, &msg, &msg_id](const PeerContextPtr &ctx) {
      assert(ctx->message_to_send);

      ctx->message_to_send->addMessage(*msg, msg_id);

      // forward immediately to those in mesh
      connectivity_.peerIsWritable(ctx, true);
    });

    subscribed_peers_.selectAll(
        [this, &msg_id, is_published_locally](const PeerContextPtr &ctx) {
          assert(ctx->message_to_send);

          ctx->message_to_send->addIHave(topic_, msg_id);

          // local messages announce themselves immediately
          connectivity_.peerIsWritable(ctx, is_published_locally);
        });

    seen_cache_.emplace_back(now, msg_id);
  }

  void RemoteSubscriptions::TopicSubscriptions::onHeartbeat(Time now) {
    if (self_subscribed_ && !subscribed_peers_.empty()) {
      // add/remove mesh members according to desired network density D
      size_t sz = mesh_peers_.size();

      if (sz < config_.D) {
        auto peers = subscribed_peers_.selectRandomPeers(config_.D - sz);
        for (auto &p : peers) {
          addToMesh(p);
          subscribed_peers_.erase(p->peer_id);
        }
      } else if (sz > config_.D) {
        auto peers = mesh_peers_.selectRandomPeers(sz - config_.D);
        for (auto &p : peers) {
          removeFromMesh(p);
          mesh_peers_.erase(p->peer_id);
        }
      }
    }

    // fanout ends some time after this host ends publishing to the topic,
    // to save space and traffic
    if (fanout_period_ends_ < now) {
      fanout_period_ends_ = 0;
    }

    // shift msg ids cache
    if (!seen_cache_.empty()) {
      auto it = std::find_if(seen_cache_.begin(), seen_cache_.end(),
                             [now](const auto& p) { return p.first >= now; });
      seen_cache_.erase(seen_cache_.begin(), it);
    }
  }

  void RemoteSubscriptions::TopicSubscriptions::onSelfSubscribed(
      bool self_subscribed) {
    self_subscribed_ = self_subscribed;
    if (!self_subscribed_) {
      // remove the mesh
      mesh_peers_.selectAll(
          [this](const PeerContextPtr &p) { removeFromMesh(p); });
      mesh_peers_.clear();
    }
  }

  void RemoteSubscriptions::TopicSubscriptions::onPeerSubscribed(
      const PeerContextPtr &p) {
    if (p->subscribed_to.count(topic_) != 0) {
      // ignore double subscription
      return;
    }

    p->subscribed_to.insert(topic_);
    subscribed_peers_.insert(p);

    // announce the peer about messages available for the topic
    for (const auto& [_, msg_id] : seen_cache_) {
      p->message_to_send->addIHave(topic_, msg_id);
    }
    // will be sent on next heartbeat
    connectivity_.peerIsWritable(p, false);
  }

  void RemoteSubscriptions::TopicSubscriptions::onPeerUnsubscribed(
      const PeerContextPtr &p) {
    auto res = subscribed_peers_.erase(p->peer_id);
    if (!res) {
      res = mesh_peers_.erase(p->peer_id);
    }
    if (res) {
      res.value()->subscribed_to.erase(topic_);
    }
  }

  void RemoteSubscriptions::TopicSubscriptions::onGraft(
      const PeerContextPtr &p) {
    auto res = mesh_peers_.find(p->peer_id);
    if (res) {
      // already there
      return;
    }
    res = subscribed_peers_.find(p->peer_id);
    if (!res) {
      res.value()->subscribed_to.insert(topic_);
    }
    if (res) {
      if (self_subscribed_) {
        mesh_peers_.insert(std::move(res.value()));
      } else {
        // we don't have mesh for the topic
        p->message_to_send->addPrune(topic_);
        connectivity_.peerIsWritable(p, true);
      }
    }
  }

  void RemoteSubscriptions::TopicSubscriptions::onPrune(
      const PeerContextPtr &p) {
    mesh_peers_.erase(p->peer_id);
  }

  void RemoteSubscriptions::TopicSubscriptions::addToMesh(
      const PeerContextPtr &p) {
    assert(p->message_to_send);

    p->message_to_send->addGraft(topic_);
    connectivity_.peerIsWritable(p, false);
    mesh_peers_.insert(p);
  }

  void RemoteSubscriptions::TopicSubscriptions::removeFromMesh(
      const PeerContextPtr &p) {
    assert(p->message_to_send);

    p->message_to_send->addPrune(topic_);
    connectivity_.peerIsWritable(p, false);
    subscribed_peers_.insert(p);
  }

}  // namespace libp2p::protocol::gossip
