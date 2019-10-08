/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "chat_protocol.hpp"

namespace custom_protocol {
  ChatProtocol::ChatProtocol(
      std::function<void(std::shared_ptr<ChatSession>)> new_session_handler,
      ChatConfig config)
      : new_session_handler_{std::move(new_session_handler)}, config_{config} {}

  libp2p::peer::Protocol ChatProtocol::getProtocolId() const {
    return "/chat/1.0.0";
  }

  void ChatProtocol::handle(StreamResult stream_res) {
    if (!stream_res) {
      return log_->error(
          "stream, which was submitted to the protocol, contains error: {}",
          stream_res.error().message());
    }

    auto session =
        std::make_shared<ChatSession>(std::move(stream_res.value(), config_));
    session->start();
    new_session_handler_(std::move(session));
  }

  outcome::result<std::shared_ptr<ChatSession>> ChatProtocol::startChatting(
      std::shared_ptr<libp2p::connection::Stream> stream) const {
    auto session = std::make_shared<ChatSession>(std::move(stream), config_);
    session->start();
    return session;
  }
}  // namespace custom_protocol
