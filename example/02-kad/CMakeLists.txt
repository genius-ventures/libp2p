#
# Copyright Soramitsu Co., Ltd. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0
#
link_directories(${Boost_LIB_DIR_STR})

add_executable(kad_peer_discovery_example
    kad_peer_discovery_example.cpp
    factory.cpp
    )

if(FORCE_MULTILE)
    set_target_properties(kad_peer_discovery_example PROPERTIES LINK_FLAGS "/FORCE:MULTIPLE")
endif()
target_link_libraries(kad_peer_discovery_example
    p2p_basic_host
    p2p_default_network
    p2p_peer_repository
    p2p_inmem_address_repository
    p2p_inmem_key_repository
    p2p_inmem_protocol_repository
    p2p_literals
    p2p_kad
    asio_scheduler
    ${WIN_CRYPT_LIBRARY}
    )
