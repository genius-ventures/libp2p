#
# Copyright Soramitsu Co., Ltd. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0
#

add_executable(libp2p_chat
    libp2p_chat.cpp
    chat_protocol.cpp
    chat_session.cpp
    )
target_link_libraries(libp2p_chat
    p2p_basic_host
    p2p_default_network
    p2p_peer_repository
    p2p_inmem_address_repository
    p2p_inmem_key_repository
    p2p_inmem_protocol_repository
    p2p_literals
    Boost::program_options
    )
