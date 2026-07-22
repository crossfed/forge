module;

#include <concepts>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

export module forge.contract.binary_extension;

import forge.contract.intrinsics;
import forge.raw.codec;

export namespace forge::contract {

template <typename T> class binary_extension {
 public:
   using value_type = T;

   constexpr binary_extension() = default;
   constexpr binary_extension(const T& value) : value_(value) {}
   constexpr binary_extension(T&& value) : value_(std::move(value)) {}

   template <typename... Args>
      requires std::constructible_from<T, Args...>
   constexpr explicit binary_extension(std::in_place_t, Args&&... args)
       : value_(std::in_place, std::forward<Args>(args)...) {}

   [[nodiscard]] constexpr explicit operator bool() const noexcept {
      return value_.has_value();
   }

   [[nodiscard]] constexpr bool has_value() const noexcept {
      return value_.has_value();
   }

   constexpr T& value() & {
      check(value_.has_value(), "cannot access empty binary extension");
      return *value_;
   }

   constexpr const T& value() const& {
      check(value_.has_value(), "cannot access empty binary extension");
      return *value_;
   }

   constexpr T&& value() && {
      check(value_.has_value(), "cannot access empty binary extension");
      return std::move(*value_);
   }

   template <typename U> [[nodiscard]] constexpr T value_or(U&& fallback) const& {
      return value_.value_or(std::forward<U>(fallback));
   }

   template <typename U> [[nodiscard]] constexpr T value_or(U&& fallback) && {
      return std::move(value_).value_or(std::forward<U>(fallback));
   }

   constexpr T* operator->() {
      return std::addressof(value());
   }

   constexpr const T* operator->() const {
      return std::addressof(value());
   }

   constexpr T& operator*() & {
      return value();
   }

   constexpr const T& operator*() const& {
      return value();
   }

   template <typename... Args> constexpr T& emplace(Args&&... args) {
      return value_.emplace(std::forward<Args>(args)...);
   }

   constexpr void reset() noexcept {
      value_.reset();
   }

 private:
   std::optional<T> value_;
};

template <typename Stream, typename T> void raw_pack(Stream& stream, const binary_extension<T>& value) {
   if (value.has_value()) {
      ::forge::raw::pack(stream, value.value());
   }
}

template <typename Stream, typename T> void raw_unpack(Stream& stream, binary_extension<T>& value) {
   if (stream.remaining() == 0U) {
      value.reset();
      return;
   }
   auto unpacked = T{};
   ::forge::raw::unpack(stream, unpacked);
   value.emplace(std::move(unpacked));
}

} // namespace forge::contract
