module;

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <typeindex>
#include <type_traits>

export module forge.api.core.context;

export import forge.api.core.types;

export namespace forge::api::core {

class request_view {
 public:
   request_view() = default;

   template <typename T> [[nodiscard]] static request_view borrow(const T& value) noexcept {
      using value_type = std::remove_cvref_t<T>;
      return request_view{std::addressof(value), typeid(value_type)};
   }

   [[nodiscard]] std::type_index type() const noexcept {
      return type_;
   }

   template <typename T> [[nodiscard]] const std::remove_cvref_t<T>* get_if() const noexcept {
      using value_type = std::remove_cvref_t<T>;
      if (type_ != typeid(value_type)) {
         return nullptr;
      }
      return static_cast<const value_type*>(value_);
   }

 private:
   request_view(const void* value, std::type_index type) noexcept : value_{value}, type_{type} {}

   const void* value_ = nullptr;
   std::type_index type_ = typeid(void);
};

struct call_context {
   call_id id;
   api_ref api;
   std::string method;
   metadata meta;
   bytes payload;
   codec_id codec;
   frame_kind kind = frame_kind::request;
   request_view request;
};

inline constexpr std::string_view trusted_metadata_prefix = "forge.";
inline constexpr std::string_view p2p_remote_peer_metadata_key = "forge.p2p.remote_peer";

[[nodiscard]] std::optional<std::string> metadata_value(const metadata& value, std::string_view key);

} // namespace forge::api
