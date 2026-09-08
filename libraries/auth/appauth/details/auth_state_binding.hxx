#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace forge::auth::appauth::detail {

struct auth_state_config {
   std::string issuer;
   std::string client_id;
   std::vector<std::string> scopes;
   std::optional<std::string> audience;
};

// This encoding is persisted alongside the opaque AppAuth archive and must remain deterministic.
[[nodiscard]] std::string canonical_auth_state_binding(const auth_state_config& config);

class auth_state_epoch {
 public:
   using ticket = std::uint64_t;

   [[nodiscard]] ticket capture() const noexcept {
      const auto lock = std::scoped_lock{mutex_};
      return generation_;
   }

   template <typename Action> [[nodiscard]] bool commit_if_current(ticket expected, Action&& action) {
      const auto lock = std::scoped_lock{mutex_};
      if (expected != generation_) {
         return false;
      }
      std::forward<Action>(action)();
      ++generation_;
      return true;
   }

   template <typename Action> decltype(auto) invalidate(Action&& action) {
      const auto lock = std::scoped_lock{mutex_};
      ++generation_;
      return std::forward<Action>(action)();
   }

 private:
   mutable std::mutex mutex_;
   ticket generation_ = 1;
};

} // namespace forge::auth::appauth::detail
