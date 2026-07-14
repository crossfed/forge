#include "details/stop_state.hxx"

#include <functional>
#include <mutex>
#include <optional>
#include <stop_token>
#include <utility>

namespace forge::asio::detail {

struct stop_state::impl {
   std::stop_source source;
   std::mutex mutex;
   std::optional<std::stop_callback<std::function<void()>>> parent_callback;
};

stop_state::stop_state() : impl_{std::make_shared<impl>()} {}
stop_state::~stop_state() = default;
stop_state::stop_state(const stop_state&) noexcept = default;
stop_state& stop_state::operator=(const stop_state&) noexcept = default;
stop_state::stop_state(stop_state&&) noexcept = default;
stop_state& stop_state::operator=(stop_state&&) noexcept = default;

bool stop_state::request_stop() noexcept {
   return impl_ != nullptr && impl_->source.request_stop();
}

bool stop_state::stop_requested() const noexcept {
   return impl_ != nullptr && impl_->source.stop_requested();
}

std::stop_token stop_state::token() const noexcept {
   return impl_ == nullptr ? std::stop_token{} : impl_->source.get_token();
}

void stop_state::link(std::stop_token parent, std::function<void()> callback) {
   if (impl_ == nullptr || !parent.stop_possible()) {
      return;
   }
   const auto lock = std::scoped_lock{impl_->mutex};
   impl_->parent_callback.emplace(std::move(parent), std::move(callback));
}

void stop_state::clear_link() noexcept {
   if (impl_ == nullptr) {
      return;
   }
   const auto lock = std::scoped_lock{impl_->mutex};
   impl_->parent_callback.reset();
}

} // namespace forge::asio::detail
