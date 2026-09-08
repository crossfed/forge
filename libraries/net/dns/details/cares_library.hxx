#pragma once

#include <memory>
#include <mutex>

namespace forge::net::dns {

class cares_library final {
 public:
   [[nodiscard]] static std::shared_ptr<cares_library> acquire();
   ~cares_library();

 private:
   struct global_state {
      std::mutex state_mutex;
      std::weak_ptr<cares_library> active;
   };

   [[nodiscard]] static global_state& state();
};

} // namespace forge::net::dns
