/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef LIBP2P_LITERALS_HPP
#define LIBP2P_LITERALS_HPP

#include <cstdint>
#include <vector>

#include <libp2p/common/types.hpp>

namespace libp2p {

  namespace multi {
    class Multiaddress;
    class Multihash;
  }  // namespace multi

  namespace peer {
    class PeerId;
  }

  namespace common {
    Hash256 operator""_hash256(const char *c, size_t s);

    std::vector<uint8_t> operator""_v(const char *c, size_t s);

    std::vector<uint8_t> operator""_unhex(const char *c, size_t s);

    multi::Multiaddress operator""_multiaddr(const char *c, size_t s);

    multi::Multihash operator""_multihash(const char *c, size_t s);

    peer::PeerId operator""_peerid(const char *c, size_t s);
  }  // namespace common

}  // namespace libp2p

#endif  // LIBP2P_LITERALS_HPP
