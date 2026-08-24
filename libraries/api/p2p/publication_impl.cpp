module;

#include <functional>
#include <mutex>
#include <utility>

module forge.api.p2p.publication;

#include "details/publication_impl.hxx"

namespace forge::api::p2p::detail {

publication_impl::publication_impl(std::function<void()> close) : close_{std::move(close)} {}

void publication_impl::close() noexcept {
   auto callback = std::function<void()>{};
   {
      auto lock = std::scoped_lock{mutex_};
      if (!active_) {
         return;
      }
      active_ = false;
      callback = std::move(close_);
   }
   try {
      if (callback) {
         callback();
      }
   } catch (...) {
      // Publication close is the noexcept shutdown boundary.
   }
}

bool publication_impl::active() const noexcept {
   auto lock = std::scoped_lock{mutex_};
   return active_;
}

} // namespace forge::api::p2p::detail
