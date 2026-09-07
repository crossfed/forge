module;

#include <forge/exceptions/macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

export module forge.crypto.symmetric.xsalsa20;

export import forge.exceptions;
import forge.crypto.core.secret_bytes;
import forge.crypto.core.types;

export namespace forge::crypto::symmetric::xsalsa20::exceptions {

enum class code : std::uint16_t {
   invalid_key = 1,
   invalid_nonce = 2,
   counter_exhausted = 3,
   invalid_state = 4,
   backend_error = 5,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.crypto.symmetric.xsalsa20")

using invalid_key = forge::exceptions::coded_exception<code, code::invalid_key>;
using invalid_nonce = forge::exceptions::coded_exception<code, code::invalid_nonce>;
using counter_exhausted = forge::exceptions::coded_exception<code, code::counter_exhausted>;
using invalid_state = forge::exceptions::coded_exception<code, code::invalid_state>;
using backend_error = forge::exceptions::coded_exception<code, code::backend_error>;

} // namespace forge::crypto::symmetric::xsalsa20::exceptions

export namespace forge::crypto::symmetric::xsalsa20 {

inline constexpr auto key_size = std::size_t{32};
inline constexpr auto nonce_size = std::size_t{24};
inline constexpr auto block_size = std::size_t{64};

struct nonce {
   std::array<std::uint8_t, nonce_size> bytes{};
};

class key {
 public:
   explicit key(std::span<const std::uint8_t> bytes);
   ~key();

   key(key&&) noexcept;
   key& operator=(key&&) noexcept;

   key(const key&) = delete;
   key& operator=(const key&) = delete;

   [[nodiscard]] std::span<const std::uint8_t> span() const & noexcept;
   [[nodiscard]] std::span<const std::uint8_t> span() const && = delete;

 private:
   core::secret_bytes value_;
};

[[nodiscard]] nonce make_nonce(std::span<const std::uint8_t> bytes);

class stream {
 public:
   stream(const key& key, nonce nonce);
   ~stream();

   stream(stream&&) noexcept;
   stream& operator=(stream&&) noexcept;

   stream(const stream&) = delete;
   stream& operator=(const stream&) = delete;

   void transform(std::span<std::uint8_t> bytes);

 private:
   struct impl;

   explicit stream(std::unique_ptr<impl> implementation) noexcept;
   static std::unique_ptr<impl> make_impl(const key& key, nonce nonce);

   std::unique_ptr<impl> impl_;
};

[[nodiscard]] core::bytes transform(const key& key, const nonce& nonce, std::span<const std::uint8_t> input);

} // namespace forge::crypto::symmetric::xsalsa20
