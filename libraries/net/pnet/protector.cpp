module;

#include <forge/exceptions/macros.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <span>
#include <stop_token>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/this_coro.hpp>

module forge.net.pnet.protector;

import forge.codec.hex;
import forge.asio.gate;
import forge.asio.notification;
import forge.crypto.core.secret_bytes;
import forge.crypto.digest.sha256;
import forge.crypto.core.random;
import forge.crypto.symmetric.xsalsa20;

#include "details/protected_stream.hxx"

namespace forge::net::pnet {
namespace {

inline constexpr auto fingerprint_domain = std::string_view{"forge-p2p-stage6-pnet-fingerprint-v1"};

using decoded_key = std::array<std::uint8_t, pre_shared_key_size>;

void erase_decoded_key(decoded_key* value) noexcept {
   if (value) {
      forge::crypto::core::secure_erase(std::span<std::uint8_t>{*value});
      delete value;
   }
}

[[noreturn]] void throw_invalid_key(std::string_view message) {
   FORGE_THROW_EXCEPTION(exceptions::invalid_options, message);
}

} // namespace

pre_shared_key::pre_shared_key(std::span<const std::uint8_t> bytes) {
   if (bytes.size() != pre_shared_key_size) {
      throw_invalid_key("pnet pre-shared key requires exactly 32 bytes");
   }
   value_ = forge::crypto::core::secret_bytes{bytes};
}

pre_shared_key::~pre_shared_key() = default;
pre_shared_key::pre_shared_key(pre_shared_key&&) noexcept = default;
pre_shared_key& pre_shared_key::operator=(pre_shared_key&&) noexcept = default;

std::span<const std::uint8_t> detail::pre_shared_key_access::bytes(const pre_shared_key& value) noexcept {
   return value.value_.span();
}

pre_shared_key pre_shared_key::parse_swarm_key(std::string_view text) {
   const auto first_line = std::string_view{"/key/swarm/psk/1.0.0/"};
   const auto second_line = std::string_view{"/base16/"};
   auto remaining = text;
   const auto take_line = [&remaining](std::string_view expected) {
      if (!remaining.starts_with(expected)) {
         return false;
      }
      remaining.remove_prefix(expected.size());
      if (remaining.starts_with("\r\n")) {
         remaining.remove_prefix(2U);
         return true;
      }
      if (remaining.starts_with('\n')) {
         remaining.remove_prefix(1U);
         return true;
      }
      return false;
   };

   if (!take_line(first_line) || !take_line(second_line) || remaining.size() < pre_shared_key_size * 2U) {
      throw_invalid_key("pnet swarm key must use the canonical base16 form");
   }

   const auto encoded = remaining.substr(0U, pre_shared_key_size * 2U);
   remaining.remove_prefix(encoded.size());
   if (!remaining.empty() && remaining != "\n" && remaining != "\r\n") {
      throw_invalid_key("pnet swarm key must contain exactly three lines");
   }

   auto decoded = std::unique_ptr<decoded_key, decltype(&erase_decoded_key)>{new decoded_key{}, erase_decoded_key};
   try {
      const auto size = forge::codec::hex::decode(encoded, std::span{*decoded});
      if (size != decoded->size()) {
         throw_invalid_key("pnet swarm key has an invalid decoded size");
      }
      return pre_shared_key{*decoded};
   } catch (const forge::codec::hex::exceptions::invalid_input&) {
      throw_invalid_key("pnet swarm key has malformed base16 data");
   }
}

operational_fingerprint pre_shared_key::fingerprint() const {
   if (value_.size() != pre_shared_key_size) {
      throw_invalid_key("pnet pre-shared key has no usable secret material");
   }

   auto encoder = forge::crypto::digest::sha256::encoder{};
   encoder.write(fingerprint_domain.data(), static_cast<std::uint32_t>(fingerprint_domain.size()));
   encoder.put('\0');
   encoder.write(value_.span());
   return {.bytes = encoder.result().extract_as_byte_array()};
}

protector::protector(pre_shared_key key) : key_{std::make_shared<const pre_shared_key>(std::move(key))} {}

protector::~protector() = default;
protector::protector(protector&&) noexcept = default;
protector& protector::operator=(protector&&) noexcept = default;

operational_fingerprint protector::fingerprint() const {
   if (!key_) {
      throw_invalid_key("pnet protector has no usable secret material");
   }
   return key_->fingerprint();
}

boost::asio::awaitable<forge::net::transport::stream_connection>
protector::async_protect(forge::net::transport::stream_connection connection, std::stop_token stop) const {
   if (!key_ || detail::pre_shared_key_access::bytes(*key_).size() != pre_shared_key_size) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "pnet protector requires a 32-byte pre-shared key");
   }

   enum class operation_state : std::uint8_t {
      active,
      canceled,
      completed,
   };
   auto state = std::atomic{operation_state::active};
   auto cancellation = std::stop_callback{stop, [&state, &stream = connection.stream]() noexcept {
                                              auto expected = operation_state::active;
                                              if (state.compare_exchange_strong(expected, operation_state::canceled,
                                                                                std::memory_order_acq_rel)) {
                                                 stream.request_cancel();
                                              }
                                           }};
   if (state.load(std::memory_order_acquire) == operation_state::canceled) {
      FORGE_THROW_EXCEPTION(exceptions::canceled, "pnet protection was canceled before nonce exchange");
   }

   const auto local_nonce = forge::crypto::core::random_array<forge::crypto::symmetric::xsalsa20::nonce_size>();
   try {
      co_await connection.stream.async_write(std::span<const std::uint8_t>{local_nonce});
   } catch (...) {
      if (state.load(std::memory_order_acquire) == operation_state::canceled) {
         FORGE_THROW_EXCEPTION(exceptions::canceled, "pnet protection was canceled during nonce exchange");
      }
      throw;
   }
   auto expected = operation_state::active;
   if (!state.compare_exchange_strong(expected, operation_state::completed, std::memory_order_acq_rel)) {
      FORGE_THROW_EXCEPTION(exceptions::canceled, "pnet protection was canceled during nonce exchange");
   }
   const auto executor = co_await boost::asio::this_coro::executor;
   co_return detail::protected_stream::wrap(std::move(connection), key_, local_nonce, executor);
}

} // namespace forge::net::pnet
