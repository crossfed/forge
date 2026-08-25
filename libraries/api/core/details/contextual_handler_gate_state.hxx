#pragma once

#include <boost/asio/awaitable.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

namespace forge::api::core::detail {

struct contextual_handler_gate_completion {
   metadata meta;
   std::optional<bytes> canonical_request;
   bool successful = false;
};

class contextual_handler_gate_state final {
 public:
   contextual_handler_gate_state(frame request, contextual_dispatch_hook before,
                                 std::function<void(const bytes&)> request_validator,
                                 std::function<void(const bytes&, const bytes&)> response_validator,
                                 std::shared_ptr<void> implementation);

   [[nodiscard]] static contextual_handler_gate
   make_gate(std::shared_ptr<contextual_handler_gate_state> state);
   boost::asio::awaitable<std::shared_ptr<void>>
   acquire(request_view request, const canonical_request_encoder& encode);
   void reject_reacquire();
   void close();
   [[nodiscard]] bool completed_successfully_once() const;
   void validate_response(const bytes& response) const;
   void transfer_completion_metadata(metadata& target);

 private:
   [[nodiscard]] bool active() const;
   void publish_failure();

   frame request_;
   contextual_dispatch_hook before_;
   std::function<void(const bytes&)> request_validator_;
   std::function<void(const bytes&, const bytes&)> response_validator_;
   std::shared_ptr<void> implementation_;
   mutable std::mutex mutex_;
   std::size_t attempts_ = 0;
   bool active_ = true;
   bool successful_ = false;
   std::optional<contextual_handler_gate_completion> completion_;
};

} // namespace forge::api::core::detail
