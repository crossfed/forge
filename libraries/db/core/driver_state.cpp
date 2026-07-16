module;

#include <forge/exceptions/macros.hpp>

#include <memory>
#include <mutex>
#include <utility>

module forge.db.core.driver;

import forge.db.core.exceptions;

#include "details/driver_state.hxx"

namespace forge::db::core::detail {

driver_state::open_admission::open_admission(std::shared_ptr<driver_state> owner) noexcept
    : owner_{std::move(owner)} {}

driver_state::open_admission::~open_admission() {
   cancel();
}

driver_state::open_admission::open_admission(open_admission&& other) noexcept
    : owner_{std::move(other.owner_)} {}

driver_state::open_admission& driver_state::open_admission::operator=(open_admission&& other) noexcept {
   if (this != &other) {
      cancel();
      owner_ = std::move(other.owner_);
   }
   return *this;
}

std::shared_ptr<driver_state> driver_state::open_admission::publish() {
   if (!owner_) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_descriptor, "db driver open admission is empty");
   }
   owner_->publish_open();
   return std::exchange(owner_, {});
}

void driver_state::open_admission::cancel() noexcept {
   if (owner_) {
      owner_->cancel_open();
      owner_.reset();
   }
}

driver_state::open_admission driver_state::admit_open() {
   auto lock = std::lock_guard{mutex_};
   if (phase_ != phase::open) {
      FORGE_THROW_EXCEPTION(exceptions::driver_closed, "db driver is closing or closed");
   }
   ++opening_;
   return open_admission{shared_from_this()};
}

void driver_state::require_open() {
   auto lock = std::lock_guard{mutex_};
   if (phase_ != phase::open) {
      FORGE_THROW_EXCEPTION(exceptions::driver_closed,
                            "db driver is closing or closed");
   }
}

driver_state::close_action driver_state::admit_close() {
   auto lock = std::lock_guard{mutex_};
   if (phase_ == phase::closed) {
      return close_action::already_closed;
   }
   phase_ = phase::closing;
   if (close_running_ || opening_ != 0 || active_ != 0) {
      FORGE_THROW_EXCEPTION(exceptions::driver_busy, "db driver still owns active or opening sessions");
   }
   close_running_ = true;
   return close_action::run;
}

void driver_state::finish_close() noexcept {
   auto lock = std::lock_guard{mutex_};
   close_running_ = false;
   phase_ = phase::closed;
}

void driver_state::fail_close() noexcept {
   auto lock = std::lock_guard{mutex_};
   close_running_ = false;
}

void driver_state::release_session() noexcept {
   auto lock = std::lock_guard{mutex_};
   if (active_ != 0) {
      --active_;
   }
}

void driver_state::cancel_open() noexcept {
   auto lock = std::lock_guard{mutex_};
   if (opening_ != 0) {
      --opening_;
   }
}

void driver_state::publish_open() {
   auto lock = std::lock_guard{mutex_};
   if (opening_ == 0) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_descriptor, "db driver open admission is not pending");
   }
   --opening_;
   ++active_;
}

} // namespace forge::db::core::detail
