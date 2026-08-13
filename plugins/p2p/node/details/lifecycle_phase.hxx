#pragma once

#include <cstdint>

namespace forge::plugins::p2p::node {

enum class lifecycle_phase : std::uint8_t {
   idle,
   starting,
   started,
   stopping,
   stopped,
};

} // namespace forge::plugins::p2p::node
