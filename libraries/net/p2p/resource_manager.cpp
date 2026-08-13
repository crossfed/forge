module;

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

module forge.net.p2p.resource_manager;

#include "details/resource_manager_state.hxx"

namespace forge::net::p2p {

resource_manager::resource_manager() : resource_manager(limits{}) {}

resource_manager::resource_manager(limits value) : state_(std::make_shared<state>(value)) {}

resource_manager::~resource_manager() = default;

const resource_manager::limits& resource_manager::configured_limits() const noexcept {
   static const auto defaults = limits{};
   return state_ ? state_->configured_limits() : defaults;
}

resource_manager::snapshot resource_manager::current() const noexcept {
   return state_ ? state_->current() : snapshot{};
}

std::optional<resource_manager::session_reservation>
resource_manager::reserve_session(session_direction direction) noexcept {
   if (!state_ || !state_->acquire_pending(direction)) {
      return std::nullopt;
   }
   return session_reservation{state_, direction};
}

std::optional<resource_manager::dial_reservation> resource_manager::reserve_dial() noexcept {
   if (!state_ || !state_->acquire_dial()) {
      return std::nullopt;
   }
   return dial_reservation{state_};
}

std::optional<resource_manager::dial_reservation> resource_manager::reserve_dial(const peer_id& peer) noexcept {
   auto reservation = reserve_dial();
   if (!reservation || !reservation->bind(peer)) {
      return std::nullopt;
   }
   return reservation;
}

std::optional<resource_manager::stream_reservation> resource_manager::reserve_stream() noexcept {
   if (!state_ || !state_->acquire_stream(false)) {
      return std::nullopt;
   }
   return stream_reservation{state_, false};
}

std::optional<resource_manager::stream_reservation> resource_manager::reserve_stream(const scope& value) noexcept {
   auto reservation = reserve_stream();
   if (!reservation || !reservation->bind(value)) {
      return std::nullopt;
   }
   return reservation;
}

std::optional<resource_manager::stream_reservation> resource_manager::reserve_relay_stream() noexcept {
   if (!state_ || !state_->acquire_stream(true)) {
      return std::nullopt;
   }
   return stream_reservation{state_, true};
}

std::optional<resource_manager::relay_reservation> resource_manager::reserve_relay(const scope& value) noexcept {
   if (!state_ || !state_->acquire_relay(value.peer)) {
      return std::nullopt;
   }
   return relay_reservation{state_, value.peer};
}

std::optional<resource_manager::queued_bytes_reservation>
resource_manager::reserve_queued_bytes(std::uint64_t bytes) noexcept {
   if (!state_ || !state_->acquire_queued_bytes(bytes)) {
      return std::nullopt;
   }
   return queued_bytes_reservation{state_, bytes};
}

bool resource_manager::record_malformed(const scope& value) noexcept {
   return state_ && state_->record_malformed(value.peer);
}

resource_manager::session_reservation::session_reservation() noexcept = default;

resource_manager::session_reservation::session_reservation(std::shared_ptr<state> owner,
                                                           session_direction direction) noexcept
    : owner_(std::move(owner)), direction_(direction) {}

resource_manager::session_reservation::~session_reservation() {
   release();
}

resource_manager::session_reservation::session_reservation(session_reservation&& other) noexcept
    : owner_(std::move(other.owner_)), direction_(other.direction_), scope_(std::move(other.scope_)) {}

resource_manager::session_reservation&
resource_manager::session_reservation::operator=(session_reservation&& other) noexcept {
   if (this != &other) {
      release();
      owner_ = std::move(other.owner_);
      direction_ = other.direction_;
      scope_ = std::move(other.scope_);
   }
   return *this;
}

bool resource_manager::session_reservation::active() const noexcept {
   return owner_ != nullptr;
}

bool resource_manager::session_reservation::established() const noexcept {
   return scope_.has_value();
}

bool resource_manager::session_reservation::establish(session_scope value) noexcept {
   if (!owner_ || scope_ || value.direction != direction_ || !owner_->establish_session(value)) {
      return false;
   }
   owner_->release_pending(direction_);
   scope_ = std::move(value);
   return true;
}

void resource_manager::session_reservation::release() noexcept {
   if (!owner_) {
      return;
   }
   if (scope_) {
      owner_->release_session(*scope_);
   } else {
      owner_->release_pending(direction_);
   }
   scope_.reset();
   owner_.reset();
}

resource_manager::dial_reservation::dial_reservation() noexcept = default;

resource_manager::dial_reservation::dial_reservation(std::shared_ptr<state> owner) noexcept
    : owner_(std::move(owner)) {}

resource_manager::dial_reservation::~dial_reservation() {
   release();
}

resource_manager::dial_reservation::dial_reservation(dial_reservation&& other) noexcept
    : owner_(std::move(other.owner_)), peer_(std::move(other.peer_)) {}

resource_manager::dial_reservation& resource_manager::dial_reservation::operator=(dial_reservation&& other) noexcept {
   if (this != &other) {
      release();
      owner_ = std::move(other.owner_);
      peer_ = std::move(other.peer_);
   }
   return *this;
}

bool resource_manager::dial_reservation::active() const noexcept {
   return owner_ != nullptr;
}

bool resource_manager::dial_reservation::bound() const noexcept {
   return peer_.has_value();
}

bool resource_manager::dial_reservation::bind(peer_id peer) noexcept {
   if (!owner_ || peer_ || !owner_->bind_dial(peer)) {
      return false;
   }
   peer_ = std::move(peer);
   return true;
}

void resource_manager::dial_reservation::release() noexcept {
   if (owner_) {
      owner_->release_dial(peer_);
      peer_.reset();
      owner_.reset();
   }
}

resource_manager::stream_reservation::stream_reservation() noexcept = default;

resource_manager::stream_reservation::stream_reservation(std::shared_ptr<state> owner, bool relay) noexcept
    : owner_(std::move(owner)), relay_(relay) {}

resource_manager::stream_reservation::~stream_reservation() {
   release();
}

resource_manager::stream_reservation::stream_reservation(stream_reservation&& other) noexcept
    : owner_(std::move(other.owner_)), scope_(std::move(other.scope_)), relay_(other.relay_) {}

resource_manager::stream_reservation&
resource_manager::stream_reservation::operator=(stream_reservation&& other) noexcept {
   if (this != &other) {
      release();
      owner_ = std::move(other.owner_);
      scope_ = std::move(other.scope_);
      relay_ = other.relay_;
   }
   return *this;
}

bool resource_manager::stream_reservation::active() const noexcept {
   return owner_ != nullptr;
}

bool resource_manager::stream_reservation::bound() const noexcept {
   return scope_.has_value();
}

bool resource_manager::stream_reservation::bind(scope value) noexcept {
   if (!owner_ || scope_ || !owner_->bind_stream(value)) {
      return false;
   }
   scope_ = std::move(value);
   return true;
}

void resource_manager::stream_reservation::release() noexcept {
   if (owner_) {
      owner_->release_stream(scope_, relay_);
      scope_.reset();
      owner_.reset();
   }
}

resource_manager::queued_bytes_reservation::queued_bytes_reservation() noexcept = default;

resource_manager::queued_bytes_reservation::queued_bytes_reservation(std::shared_ptr<state> owner,
                                                                     std::uint64_t bytes) noexcept
    : owner_(std::move(owner)), bytes_(bytes) {}

resource_manager::queued_bytes_reservation::~queued_bytes_reservation() {
   release();
}

resource_manager::queued_bytes_reservation::queued_bytes_reservation(queued_bytes_reservation&& other) noexcept
    : owner_(std::move(other.owner_)), bytes_(std::exchange(other.bytes_, 0)) {}

resource_manager::queued_bytes_reservation&
resource_manager::queued_bytes_reservation::operator=(queued_bytes_reservation&& other) noexcept {
   if (this != &other) {
      release();
      owner_ = std::move(other.owner_);
      bytes_ = std::exchange(other.bytes_, 0);
   }
   return *this;
}

bool resource_manager::queued_bytes_reservation::active() const noexcept {
   return owner_ != nullptr;
}

std::uint64_t resource_manager::queued_bytes_reservation::bytes() const noexcept {
   return bytes_;
}

void resource_manager::queued_bytes_reservation::release() noexcept {
   if (owner_) {
      owner_->release_queued_bytes(bytes_);
      owner_.reset();
      bytes_ = 0;
   }
}

resource_manager::relay_reservation::relay_reservation() noexcept = default;

resource_manager::relay_reservation::relay_reservation(std::shared_ptr<state> owner, peer_id peer) noexcept
    : owner_(std::move(owner)), peer_(std::move(peer)) {}

resource_manager::relay_reservation::~relay_reservation() {
   release();
}

resource_manager::relay_reservation::relay_reservation(relay_reservation&& other) noexcept
    : owner_(std::move(other.owner_)), peer_(std::move(other.peer_)) {}

resource_manager::relay_reservation&
resource_manager::relay_reservation::operator=(relay_reservation&& other) noexcept {
   if (this != &other) {
      release();
      owner_ = std::move(other.owner_);
      peer_ = std::move(other.peer_);
   }
   return *this;
}

bool resource_manager::relay_reservation::active() const noexcept {
   return owner_ != nullptr;
}

void resource_manager::relay_reservation::release() noexcept {
   if (owner_) {
      owner_->release_relay(peer_);
      owner_.reset();
   }
}

} // namespace forge::net::p2p
