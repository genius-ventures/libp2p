/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef LIBP2P_CHAT_CONFIG_HPP
#define LIBP2P_CHAT_CONFIG_HPP

#include <iostream>
#include <ostream>

#include <libp2p/event/bus.hpp>

namespace custom_protocol {
  /**
   * Config for Chat protocol
   */
  struct ChatConfig {
    /// event bus, to which errors are reported
    libp2p::event::Bus &bus;

    /// maximum size of one message
    size_t max_message_size = 2u << 22u;  // 4 MB

    /// stream, to which Chat will write output; cout by default, but can also
    /// be, for example, file stream or something else
    std::ostream &out_stream = std::cout;
  };
}  // namespace custom_protocol

#endif  // LIBP2P_CHAT_CONFIG_HPP
