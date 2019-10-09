/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef LIBP2P_CHAT_SESSION_HPP
#define LIBP2P_CHAT_SESSION_HPP

#include <memory>
#include <queue>
#include <string_view>
#include <vector>

#include <libp2p/common/logger.hpp>
#include <libp2p/connection/stream.hpp>
#include <libp2p/outcome/outcome.hpp>
#include "chat_config.hpp"

namespace custom_protocol {
  /**
   * Events to be distributed over the bus must be declared as follows
   */
  namespace event {
    struct MessageWriteError {};
    using MessageWriteErrorChannel =
        libp2p::event::channel_decl<MessageWriteError, std::error_code>;

    struct MessageReadError {};

    using MessageReadErrorChannel =
        libp2p::event::channel_decl<MessageReadError, std::error_code>;
  }  // namespace event

  /**
   * Chat session between our node and the other; in case of error, it will be
   * reported over the event bus
   */
  class ChatSession : public std::enable_shared_from_this<ChatSession> {
    using ByteArray = std::vector<uint8_t>;

   public:
    /**
     * Create a new chat session
     * @param stream, over which the session is to be created
     * @param config of this chat session
     */
    ChatSession(std::shared_ptr<libp2p::connection::Stream> stream,
                ChatConfig config);

    /**
     * Start this session
     */
    void start();

    /**
     * Stop this session - it will reset the underlying stream, so all other
     * methods of the class must not be called after stopping the session
     */
    void stop();

    enum class Error {
      STREAM_CLOSED_FOR_READS = 1,  // 0 is reserved for success
      STREAM_CLOSED_FOR_WRITES
    };

   private:
    /**
     * Read message from the chat and write it into the output stream
     */
    void readNextMessage();

    /**
     * Ask the user to write message into the console and send it to the other
     * side
     */
    void writeNextMessage();

    /**
     * Write next queued message to the chat
     */
    void doWrite();

    std::shared_ptr<libp2p::connection::Stream> stream_;
    ChatConfig config_;
    ByteArray read_buffer_;

    bool active_ = false;
    bool writing_ = false;
    std::queue<std::pair<std::string, ByteArray>> write_queue_;
    libp2p::common::Logger log_ = libp2p::common::createLogger("ChatSession");
  };
}  // namespace custom_protocol

/// errors, which are placed into outcome::result, must be declared as follows
OUTCOME_HPP_DECLARE_ERROR(custom_protocol, ChatSession::Error)

#endif  // LIBP2P_CHAT_SESSION_HPP
