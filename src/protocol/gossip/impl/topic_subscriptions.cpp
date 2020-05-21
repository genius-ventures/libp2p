/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

// #include <libp2p/protocol/gossip/impl/topic_subscriptions.hpp>

#include <algorithm>
#include <cassert>

#include <libp2p/protocol/gossip/impl/connectivity.hpp>
#include <libp2p/protocol/gossip/impl/message_builder.hpp>
#include <libp2p/protocol/gossip/impl/topic_subscriptions.hpp>
namespace libp2p::protocol::gossip {

  namespace {

    // dont forward message to peer it was received from as well as to its
    // original issuer
    bool needToForward(const PeerContextPtr &ctx,
                       const boost::optional<PeerContextPtr> &from,
                       const outcome::result<peer::PeerId> &origin) {
      if (from && ctx->peer_id == from.value()->peer_id) {
        return false;
      }
      return !(origin && ctx->peer_id == origin.value());
    }

  }  // namespace

  TopicSubscriptions::TopicSubscriptions(TopicId topic, const Config &config,
                                         Connectivity &connectivity,
                                         SubLogger &log)
      : topic_(std::move(topic)),
        config_(config),
        connectivity_(connectivity),
        self_subscribed_(false),
        fanout_period_ends_(0),
        log_(log) {}

  bool TopicSubscriptions::empty() const {
    // return (not self_subscribed_) && (fanout_period_ends_ == 0)
    //     && subscribed_peers_.empty() && mesh_peers_.empty();
    return (! self_subscribed_) && (fanout_period_ends_ == 0)
        && subscribed_peers_.empty() && mesh_peers_.empty();
  }

  void TopicSubscriptions::onNewMessage(
      const boost::optional<PeerContextPtr> &from, const TopicMessage::Ptr &msg,
      const MessageId &msg_id, Time now) {
    bool is_published_locally = !from.has_value();

    if (is_published_locally) {
      fanout_period_ends_ = now + config_.seen_cache_lifetime_msec;
      log_.debug("setting fanout period for {}, {}->{}", topic_, now,
                 fanout_period_ends_);
    }

    auto origin = peerFrom(*msg);

    mesh_peers_.selectAll(
        [this, &msg, &msg_id, &from, &origin](const PeerContextPtr &ctx) {
          assert(ctx->message_to_send);

          if (needToForward(ctx, from, origin)) {
            ctx->message_to_send->addMessage(*msg, msg_id);

            // forward immediately to those in mesh
            connectivity_.peerIsWritable(ctx, true);
          }
        });

    subscribed_peers_.selectAll([this, &msg_id, &from, is_published_locally,
                                 &origin](const PeerContextPtr &ctx) {
      assert(ctx->message_to_send);

      if (needToForward(ctx, from, origin)) {
        ctx->message_to_send->addIHave(topic_, msg_id);

        // local messages announce themselves immediately
        connectivity_.peerIsWritable(ctx, is_published_locally);
      }
    });

    seen_cache_.emplace_back(now + config_.seen_cache_lifetime_msec, msg_id);

    log_.debug("message forwarded, topic={}, m={}, s={}", topic_,
               mesh_peers_.size(), subscribed_peers_.size());
  }

  void TopicSubscriptions::onHeartbeat(Time now) {
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
    if (fanout_period_ends_ != 0 && fanout_period_ends_ < now) {
      fanout_period_ends_ = 0;
      log_.debug("fanout period reset for {}", topic_);
    }

    // shift msg ids cache
    if (!seen_cache_.empty()) {
      auto it = std::find_if(seen_cache_.begin(), seen_cache_.end(),
                             [now](const auto &p) { return p.first >= now; });
      if (it != seen_cache_.begin()) {
        seen_cache_.erase(seen_cache_.begin(), it);
        log_.debug("seen cache size={} for {}", seen_cache_.size(), topic_);
      }
    }
  }

  void TopicSubscriptions::onSelfSubscribed(bool self_subscribed) {
    self_subscribed_ = self_subscribed;
    if (!self_subscribed_) {
      // remove the mesh
      log_.debug("removing mesh for {}", topic_);
      mesh_peers_.selectAll(
          [this](const PeerContextPtr &p) { removeFromMesh(p); });
      mesh_peers_.clear();
    }
  }

  void TopicSubscriptions::onPeerSubscribed(const PeerContextPtr &p) {
    assert(p->subscribed_to.count(topic_) != 0);

    subscribed_peers_.insert(p);

    // announce the peer about messages available for the topic
    for (const auto &[_, msg_id] : seen_cache_) {
      p->message_to_send->addIHave(topic_, msg_id);
    }
    // will be sent on next heartbeat
    connectivity_.peerIsWritable(p, false);
  }

  void TopicSubscriptions::onPeerUnsubscribed(const PeerContextPtr &p) {
    auto res = subscribed_peers_.erase(p->peer_id);
    if (!res) {
      res = mesh_peers_.erase(p->peer_id);
    }
  }

  void TopicSubscriptions::onGraft(const PeerContextPtr &p) {
    auto res = mesh_peers_.find(p->peer_id);
    if (res) {
      // already there
      return;
    }

    if (!subscribed_peers_.contains(p->peer_id)) {
      // subscribe first
      p->subscribed_to.insert(topic_);
      onPeerSubscribed(p);
    }

    if (self_subscribed_) {
      mesh_peers_.insert(p);
      subscribed_peers_.erase(p->peer_id);
    } else {
      // we don't have mesh for the topic
      p->message_to_send->addPrune(topic_);
      connectivity_.peerIsWritable(p, true);
    }
  }

  void TopicSubscriptions::onPrune(const PeerContextPtr &p) {
    mesh_peers_.erase(p->peer_id);
    if (p->subscribed_to.count(topic_) != 0) {
      subscribed_peers_.insert(p);
    }
  }

  void TopicSubscriptions::addToMesh(const PeerContextPtr &p) {
    assert(p->message_to_send);

    p->message_to_send->addGraft(topic_);
    connectivity_.peerIsWritable(p, false);
    mesh_peers_.insert(p);
    log_.debug("peer {} added to mesh (size={}) for topic {}", p->str,
               mesh_peers_.size(), topic_);
  }

  void TopicSubscriptions::removeFromMesh(const PeerContextPtr &p) {
    assert(p->message_to_send);

    p->message_to_send->addPrune(topic_);
    connectivity_.peerIsWritable(p, false);
    subscribed_peers_.insert(p);
    log_.debug("peer {} removed from mesh (size={}) for topic {}", p->str,
               mesh_peers_.size(), topic_);
  }

}  // namespace libp2p::protocol::gossip
