#pragma once

namespace forge::plugins::db::store {

enum class phase : std::uint8_t {
   registered,
   configured,
   initialized,
   starting,
   ready,
   started,
   stopping,
   stopped,
};

} // namespace forge::plugins::db::store
