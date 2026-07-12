#pragma once

namespace forge::plugins::db::rocksdb {

enum class phase : std::uint8_t {
   registered,
   configured,
   initialized,
   started,
   stopping,
   stopped,
};

} // namespace forge::plugins::db::rocksdb
