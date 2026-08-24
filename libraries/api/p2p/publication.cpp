module;

#include <functional>
#include <memory>
#include <mutex>
#include <utility>

module forge.api.p2p.publication;

#include "details/publication_impl.hxx"

namespace forge::api::p2p {

publication::publication(std::function<void()> close)
    : impl_{std::make_shared<detail::publication_impl>(std::move(close))} {}

publication::~publication() {
   close();
}

publication::publication(publication&& other) noexcept = default;

publication& publication::operator=(publication&& other) noexcept {
   if (this != &other) {
      close();
      impl_ = std::move(other.impl_);
   }
   return *this;
}

bool publication::active() const noexcept {
   return impl_ && impl_->active();
}

void publication::close() noexcept {
   if (impl_) {
      impl_->close();
   }
}

publication detail::publication_access::make(std::function<void()> close) {
   return publication{std::move(close)};
}

std::function<void()> detail::publication_access::close_callback(const publication& value) {
   auto impl = value.impl_;
   return [impl = std::move(impl)] {
      if (impl) {
         impl->close();
      }
   };
}

} // namespace forge::api::p2p
