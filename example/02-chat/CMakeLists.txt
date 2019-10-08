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
    p2p
    )
