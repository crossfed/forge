module;

#include <utility>

export module forge.contract.ignore;

import forge.raw.codec;

export namespace forge::contract {

template <typename T> struct ignore {};

template <typename T> struct ignore_wrapper {
   constexpr ignore_wrapper() = default;
   constexpr ignore_wrapper(T value) : value(std::move(value)) {}
   constexpr ignore_wrapper(ignore<T>) {}

   [[nodiscard]] constexpr T& get() noexcept {
      return value;
   }

   [[nodiscard]] constexpr const T& get() const noexcept {
      return value;
   }

   constexpr operator T&() noexcept {
      return value;
   }

   constexpr operator const T&() const noexcept {
      return value;
   }

   constexpr operator ignore<T>() const noexcept {
      return {};
   }

   T value{};
};

template <typename Stream, typename T> void raw_pack(Stream& stream, const ignore_wrapper<T>& value) {
   ::forge::raw::pack(stream, value.value);
}

template <typename Stream, typename T> void raw_unpack(Stream& stream, ignore_wrapper<T>& value) {
   ::forge::raw::unpack(stream, value.value);
}

template <typename Stream, typename T> void raw_pack(Stream&, const ignore<T>&) {}
template <typename Stream, typename T> void raw_unpack(Stream&, ignore<T>&) {}

} // namespace forge::contract
