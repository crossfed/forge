#pragma once

#include <functional>
#include <mutex>

namespace forge::api::p2p::detail {

class publication_impl {
 public:
   explicit publication_impl(std::function<void()> close);

   void close() noexcept;
   [[nodiscard]] bool active() const noexcept;

 private:
   mutable std::mutex mutex_;
   std::function<void()> close_;
   bool active_ = true;
};

} // namespace forge::api::p2p::detail
