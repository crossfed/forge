module;

#include <forge/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>

#include <cerrno>
#include <coroutine>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

module forge.crypto.signer.configured_provider;

import forge.crypto.core.secret_bytes;
import forge.exceptions;

#include "details/configured_provider_impl.hxx"

namespace forge::crypto::signer {
namespace {

inline constexpr auto maximum_private_key_bytes = std::size_t{4096};

void validate_id(const key_id& id) {
   if (id.value.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_config, "configured signer key id must not be empty");
   }
}

[[nodiscard]] asymmetric::private_key parse_private_key(core::secret_string source,
                                                        const asymmetric::encoding& encoding) {
   if (source.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_source, "configured signer private key must not be empty");
   }
   if (source.size() > maximum_private_key_bytes) {
      FORGE_THROW_EXCEPTION(exceptions::source_too_large, "configured signer private key exceeds size limit");
   }

   try {
      return encoding.parse_private(source.view());
   } catch (const forge::exceptions::base&) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_key, "configured signer private key is invalid");
   }
}

#if !defined(_WIN32)
class descriptor {
 public:
   explicit descriptor(int value) noexcept : value_{value} {}
   ~descriptor() {
      if (value_ >= 0) {
         static_cast<void>(::close(value_));
      }
   }

   descriptor(const descriptor&) = delete;
   descriptor& operator=(const descriptor&) = delete;

   [[nodiscard]] int get() const noexcept {
      return value_;
   }

 private:
   int value_ = -1;
};

[[noreturn]] void throw_io(std::string_view operation) {
   FORGE_THROW_EXCEPTION(exceptions::io_error, "configured signer private key file operation failed",
                         forge::exceptions::ctx("operation", operation), forge::exceptions::ctx("error", errno));
}
#endif

[[nodiscard]] core::secret_string read_private_key_file(const std::filesystem::path& path,
                                                        private_key_file_options options) {
   if (path.empty() || options.max_bytes == 0 || options.max_bytes > maximum_private_key_bytes) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_config, "configured signer private key file options are invalid");
   }

#if !defined(_WIN32)
   auto flags = O_RDONLY | O_CLOEXEC | O_NONBLOCK;
#ifdef O_NOFOLLOW
   flags |= O_NOFOLLOW;
#else
   FORGE_THROW_EXCEPTION(exceptions::unavailable, "configured signer private key files require O_NOFOLLOW support");
#endif
   auto input = descriptor{::open(path.c_str(), flags)};
   if (input.get() < 0) {
      if (errno == ELOOP) {
         FORGE_THROW_EXCEPTION(exceptions::insecure_permissions,
                               "configured signer private key file must not be a symbolic link");
      }
      throw_io("open");
   }

   struct stat status{};
   if (::fstat(input.get(), &status) != 0) {
      throw_io("fstat");
   }
   if (!S_ISREG(status.st_mode) || status.st_uid != ::geteuid() || (status.st_mode & 07777U) != 0400U) {
      FORGE_THROW_EXCEPTION(
          exceptions::insecure_permissions,
          "configured signer private key file must be a regular file owned by the current user with mode 0400");
   }
   if (status.st_size < 0 || static_cast<std::uint64_t>(status.st_size) > options.max_bytes) {
      FORGE_THROW_EXCEPTION(exceptions::source_too_large, "configured signer private key file exceeds size limit");
   }

   auto value = std::string{};
   try {
      value.resize(static_cast<std::size_t>(status.st_size));
      auto offset = std::size_t{};
      while (offset < value.size()) {
         const auto count = ::read(input.get(), value.data() + offset, value.size() - offset);
         if (count < 0 && errno == EINTR) {
            continue;
         }
         if (count <= 0) {
            if (count == 0) {
               errno = EIO;
            }
            throw_io("read");
         }
         offset += static_cast<std::size_t>(count);
      }

      auto extra = char{};
      const auto erase_extra = [&] {
         core::secure_erase(std::span<std::uint8_t>{reinterpret_cast<std::uint8_t*>(&extra), 1U});
      };
      for (;;) {
         const auto count = ::read(input.get(), &extra, 1U);
         if (count < 0 && errno == EINTR) {
            continue;
         }
         if (count < 0) {
            erase_extra();
            throw_io("read");
         }
         if (count != 0) {
            erase_extra();
            FORGE_THROW_EXCEPTION(exceptions::source_too_large,
                                  "configured signer private key file changed while being read");
         }
         erase_extra();
         break;
      }
      if (value.find('\0') != std::string::npos) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_key, "configured signer private key file contains invalid data");
      }

      if (!value.empty() && value.back() == '\n') {
         value.pop_back();
         if (!value.empty() && value.back() == '\r') {
            value.pop_back();
         }
      }
      return core::secret_string{std::move(value)};
   } catch (...) {
      core::secure_erase(value);
      throw;
   }
#else
   FORGE_THROW_EXCEPTION(exceptions::unavailable,
                         "configured signer private key files are unavailable on this platform");
#endif
}

} // namespace

configured_provider::configured_provider(std::unique_ptr<impl> implementation) : impl_(std::move(implementation)) {}

configured_provider::~configured_provider() = default;

std::shared_ptr<configured_provider> configured_provider::create(configured_provider_options options,
                                                                 const asymmetric::encoding& encoding) {
   if (options.private_key.has_value() == options.private_key_file.has_value()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_config, "configured signer requires exactly one private key source");
   }
   if (options.private_key) {
      return from_private_key(std::move(options.id), std::move(*options.private_key), encoding);
   }
   return from_private_key_file(std::move(options.id), *options.private_key_file, options.file, encoding);
}

std::shared_ptr<configured_provider> configured_provider::from_private_key(key_id id, core::secret_string private_key,
                                                                           const asymmetric::encoding& encoding) {
   validate_id(id);
   auto parsed = parse_private_key(std::move(private_key), encoding);
   auto public_key = parsed.get_public_key();
   return std::shared_ptr<configured_provider>{
       new configured_provider{std::make_unique<impl>(std::move(id), std::move(parsed), std::move(public_key))}};
}

std::shared_ptr<configured_provider> configured_provider::from_private_key_file(key_id id,
                                                                                const std::filesystem::path& path,
                                                                                private_key_file_options options,
                                                                                const asymmetric::encoding& encoding) {
   return from_private_key(std::move(id), read_private_key_file(path, options), encoding);
}

boost::asio::awaitable<std::vector<key_info>> configured_provider::keys() {
   co_return std::vector<key_info>{{.id = impl_->id, .public_key = impl_->public_key}};
}

boost::asio::awaitable<key_info> configured_provider::describe(const key_id& id) {
   if (id != impl_->id) {
      FORGE_THROW_EXCEPTION(exceptions::unknown_key, "configured signer key id is not available");
   }
   co_return key_info{.id = impl_->id, .public_key = impl_->public_key};
}

boost::asio::awaitable<sign_digest_response> configured_provider::sign_digest(sign_digest_request request) {
   if (request.id != impl_->id) {
      FORGE_THROW_EXCEPTION(exceptions::unknown_key, "configured signer key id is not available");
   }
   co_return sign_digest_response{
       .public_key = impl_->public_key,
       .signature = impl_->private_key.sign_digest(request.digest),
   };
}

} // namespace forge::crypto::signer
