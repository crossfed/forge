#pragma once

#include <utility>

namespace forge::db::blob::detail {

class snapshot_access {
 public:
   template <typename Snapshot, typename... Args>
   [[nodiscard]] static Snapshot make(Args&&... args) {
      return Snapshot(std::forward<Args>(args)...);
   }
};

} // namespace forge::db::blob::detail
