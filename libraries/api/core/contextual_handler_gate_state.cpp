module;

#include <boost/asio/awaitable.hpp>

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

module forge.api.core.descriptor;

#include "details/contextual_handler_gate_state.hxx"

namespace forge::api::core::detail {

contextual_handler_gate_state::contextual_handler_gate_state(
   frame request, contextual_dispatch_hook before,
   std::function<void(const bytes&)> request_validator,
   std::function<void(const bytes&, const bytes&)> response_validator,
   std::shared_ptr<void> implementation)
   : request_{std::move(request)}, before_{std::move(before)},
     request_validator_{std::move(request_validator)}, response_validator_{std::move(response_validator)},
     implementation_{std::move(implementation)} {}

contextual_handler_gate
contextual_handler_gate_state::make_gate(std::shared_ptr<contextual_handler_gate_state> state) {
   return contextual_handler_gate{std::move(state)};
}

boost::asio::awaitable<std::shared_ptr<void>>
contextual_handler_gate_state::acquire(request_view request,
                                       const canonical_request_encoder& encode) {
   {
      const auto lock = std::scoped_lock{mutex_};
      if (!active_) {
         throw exceptions::protocol_error{"contextual API handler gate is no longer active"};
      }
      if (++attempts_ != 1U) {
         throw exceptions::protocol_error{"contextual API invoker acquired the handler gate more than once"};
      }
   }

   try {
      if (before_) {
         co_await before_(request_, request, encode);
      }
      if (!active()) {
         throw exceptions::protocol_error{"contextual API handler gate closed before authorization completed"};
      }

      auto canonical_request = std::optional<bytes>{};
      if (request_validator_ || response_validator_) {
         canonical_request.emplace(encode());
         if (request_validator_) {
            request_validator_(*canonical_request);
         }
      }

      auto completion = contextual_handler_gate_completion{
         .meta = request_.meta,
         .canonical_request = response_validator_ ? std::move(canonical_request) : std::nullopt,
         .successful = true,
      };
      {
         const auto lock = std::scoped_lock{mutex_};
         if (!active_) {
            throw exceptions::protocol_error{"contextual API handler gate closed before authorization completed"};
         }
         completion_ = std::move(completion);
         successful_ = true;
      }
   } catch (...) {
      publish_failure();
      throw;
   }

   co_return implementation_;
}

void contextual_handler_gate_state::reject_reacquire() {
   const auto lock = std::scoped_lock{mutex_};
   ++attempts_;
}

void contextual_handler_gate_state::close() {
   const auto lock = std::scoped_lock{mutex_};
   active_ = false;
}

bool contextual_handler_gate_state::completed_successfully_once() const {
   const auto lock = std::scoped_lock{mutex_};
   return attempts_ == 1U && successful_;
}

void contextual_handler_gate_state::validate_response(const bytes& response) const {
   if (!response_validator_) {
      return;
   }
   const auto canonical_request = [&]() -> const bytes* {
      const auto lock = std::scoped_lock{mutex_};
      if (!completion_ || !completion_->canonical_request) {
         return nullptr;
      }
      return &*completion_->canonical_request;
   }();
   if (canonical_request == nullptr) {
      throw exceptions::protocol_error{"contextual API handler gate did not retain a canonical request"};
   }
   response_validator_(*canonical_request, response);
}

void contextual_handler_gate_state::transfer_completion_metadata(metadata& target) {
   const auto lock = std::scoped_lock{mutex_};
   if (completion_) {
      target = std::move(completion_->meta);
   }
}

bool contextual_handler_gate_state::active() const {
   const auto lock = std::scoped_lock{mutex_};
   return active_;
}

void contextual_handler_gate_state::publish_failure() {
   auto completion = contextual_handler_gate_completion{
      .meta = request_.meta,
      .successful = false,
   };
   const auto lock = std::scoped_lock{mutex_};
   if (active_ && !completion_) {
      completion_ = std::move(completion);
   }
}

} // namespace forge::api::core::detail
