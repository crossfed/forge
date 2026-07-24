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

struct inherited_name {
   static constexpr forge::chain::protocol::action_name get_name() {
      constexpr auto value = forge::chain::protocol::make_name("inherited");
      return value;
   }
};

struct inherited_named_request : inherited_name {
   std::uint64_t value = 0;
};

struct runtime_named_request {
   std::uint64_t value = 0;

   static forge::chain::protocol::action_name get_name() {
      return forge::chain::protocol::make_name("dynamicname");
   }
};

struct defaulted_named_request {
   std::uint64_t value = 0;

   static constexpr forge::chain::protocol::action_name get_name(std::uint64_t = 0) {
      return forge::chain::protocol::make_name("defaultname");
   }
};

struct hidden_inherited_named_request : inherited_name {
   std::uint64_t value = 0;

   std::uint64_t get_name(std::uint64_t fallback) const {
      return value == 0 ? fallback : value;
   }
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

   [[forge::action]] void restore(inherited_named_request request) {
      static_cast<void>(request);
   }

   [[forge::action]] void runtime(runtime_named_request request) {
      static_cast<void>(request);
   }

   [[forge::action]] void defaulted(defaulted_named_request request) {
      static_cast<void>(request);
   }

   [[forge::action]] void hidden(hidden_inherited_named_request request) {
      static_cast<void>(request);
   }
};
