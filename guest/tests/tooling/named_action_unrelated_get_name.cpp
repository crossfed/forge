#include <cstdint>

import forge.contract;

struct legacy_request {
   std::uint64_t value = 0;

   std::uint64_t get_name() const {
      return value;
   }
};

struct overloaded_named_request {
   std::uint64_t value = 0;

   static constexpr forge::chain::protocol::action_name get_name() {
      return forge::chain::protocol::make_name("publishrev");
   }

   std::uint64_t get_name(std::uint64_t fallback) const {
      return value == 0 ? fallback : value;
   }
};

struct inaccessible_named_request {
   std::uint64_t value = 0;

 private:
   static constexpr forge::chain::protocol::action_name get_name() {
      return forge::chain::protocol::make_name("privatehook");
   }
};

struct deleted_named_request {
   std::uint64_t value = 0;

   static constexpr forge::chain::protocol::action_name get_name() = delete;
};

class [[forge::contract("namedhelpers")]] named_action_helpers_contract : public forge::contract::context {
 public:
   using context::context;

   [[forge::action]] void submit(legacy_request request) {
      static_cast<void>(request);
   }

   [[forge::action]] void publish(overloaded_named_request request) {
      static_cast<void>(request);
   }

   [[forge::action]] void archive(inaccessible_named_request request) {
      static_cast<void>(request);
   }

   [[forge::action]] void erase(deleted_named_request request) {
      static_cast<void>(request);
   }
};
