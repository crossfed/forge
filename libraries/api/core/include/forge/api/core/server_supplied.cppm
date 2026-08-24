module;

#include <concepts>
#include <tuple>
#include <type_traits>
#include <utility>

export module forge.api.core.server_supplied;

export import forge.api.core.exceptions;
export import forge.api.core.types;

import forge.reflect.reflect;

export namespace forge::api::core {

struct trusted_invocation {
   metadata metadata;
};

template <typename Field> struct server_supplied {
   using default_marker = void;
};

} // namespace forge::api::core

namespace forge::api::core::detail {

template <typename T, typename = void> struct is_tuple_like : std::false_type {};

template <typename T>
struct is_tuple_like<T, std::void_t<decltype(std::tuple_size<T>::value)>> : std::true_type {};

template <typename Field>
concept declared_server_supplied = !requires {
   typename server_supplied<std::remove_cvref_t<Field>>::default_marker;
};

template <typename Field>
concept valid_server_supplied = requires(Field& field, const trusted_invocation& trusted) {
   { server_supplied<Field>::required } -> std::convertible_to<bool>;
   { server_supplied<Field>::reset(field) } -> std::same_as<void>;
   { server_supplied<Field>::apply(field, trusted) } -> std::same_as<bool>;
};

template <typename Field> consteval void validate_server_supplied() {
   static_assert(valid_server_supplied<Field>,
                 "server_supplied specializations require required, reset(Field&) and "
                 "bool apply(Field&, const trusted_invocation&)");
}

template <typename Request, typename Visitor>
void visit_server_supplied(Request& request, Visitor& visitor) {
   using request_type = std::remove_cvref_t<Request>;
   if constexpr (declared_server_supplied<request_type>) {
      validate_server_supplied<request_type>();
      visitor(request);
   } else if constexpr (is_tuple_like<request_type>::value) {
      std::apply(
          [&visitor](auto&... members) {
             (visit_server_supplied(members, visitor), ...);
          },
          request);
   } else if constexpr (forge::reflect::is_described_object_v<request_type>) {
      forge::reflect::for_each_member<request_type>(
          [&request, &visitor](const char*, auto member) {
             visit_server_supplied(request.*member, visitor);
          });
   }
}

} // namespace forge::api::core::detail

export namespace forge::api::core {

template <typename Request> void reset_server_supplied(Request& request) {
   auto reset = []<typename Value>(Value& value) {
      using value_type = std::remove_cvref_t<Value>;
      server_supplied<value_type>::reset(value);
   };
   detail::visit_server_supplied(request, reset);
}

template <typename Request>
void apply_server_supplied(Request& request, const trusted_invocation& trusted) {
   auto apply = [&trusted]<typename Value>(Value& value) {
      using value_type = std::remove_cvref_t<Value>;
      if (!server_supplied<value_type>::apply(value, trusted) && server_supplied<value_type>::required) {
         throw exceptions::server_supplied_unavailable{"required server-supplied request value is unavailable"};
      }
   };
   detail::visit_server_supplied(request, apply);
}

} // namespace forge::api::core
