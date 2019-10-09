/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef LIBP2P_CHAT_PROTOCOL_HPP
#define LIBP2P_CHAT_PROTOCOL_HPP

#include <functional>
#include <memory>

#include <libp2p/common/logger.hpp>
#include <libp2p/outcome/outcome.hpp>
#include <libp2p/protocol/base_protocol.hpp>
#include "chat_config.hpp"
#include "chat_session.hpp"

namespace custom_protocol {
  /**
   * It is recommended to implement new user-level protocols by inheriting from
   * the BaseProtocol, as it allows to bind such protocols into the Libp2p Host
   * object directly
   */
  class ChatProtocol : public libp2p::protocol::BaseProtocol {
   public:
    /**
     * Create an instance of Chat protocol
     * @param config, with which each chat session will be created
     */
    explicit ChatProtocol(ChatConfig config);

    libp2p::peer::Protocol getProtocolId() const override;

    /// "server" side of the protocol - how it should react to incoming streams
    /// over this protocol?
    void handle(StreamResult stream_res) override;

    /// "client" side of the protocol - what should it do, if we want to start
    /// communicating?
    outcome::result<std::shared_ptr<ChatSession>> startChatting(
        std::shared_ptr<libp2p::connection::Stream> stream) const;

   private:
    ChatConfig config_;
    libp2p::common::Logger log_ = libp2p::common::createLogger("ChatProtocol");

    std::vector<std::shared_ptr<ChatSession>> sessions_;
  };
}  // namespace custom_protocol

#endif  // LIBP2P_CHAT_PROTOCOL_HPP
