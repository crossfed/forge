module;

#include <forge/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

module forge.auth.workload.projected_token_file;

import forge.crypto.core.secret_bytes;

namespace forge::auth::workload {
namespace {

constexpr auto maximum_projected_token_bytes = std::size_t{1024U * 1024U};
constexpr auto maximum_rotation_attempts = std::size_t{16};

void validate_options(const projected_token_file_options& options) {
   if (options.path.empty() || options.max_bytes == 0U || options.max_bytes > maximum_projected_token_bytes ||
       options.max_rotation_attempts == 0U || options.max_rotation_attempts > maximum_rotation_attempts) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "projected workload token options are invalid");
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

   descriptor(descriptor&& other) noexcept : value_{std::exchange(other.value_, -1)} {}

   descriptor& operator=(descriptor&& other) noexcept {
      if (this != &other) {
         if (value_ >= 0) {
            static_cast<void>(::close(value_));
         }
         value_ = std::exchange(other.value_, -1);
      }
      return *this;
   }

   [[nodiscard]] int get() const noexcept {
      return value_;
   }

 private:
   int value_ = -1;
};

struct descriptor_identity {
   dev_t device{};
   ino_t inode{};

   [[nodiscard]] bool operator==(const descriptor_identity&) const = default;
};

struct source_identity {
   descriptor_identity object;
   off_t size{};
   std::int64_t modified_seconds{};
   long modified_nanoseconds{};

   [[nodiscard]] bool operator==(const source_identity&) const = default;
};

[[nodiscard]] descriptor_identity identify_descriptor(const struct stat& status) {
   return {
       .device = status.st_dev,
       .inode = status.st_ino,
   };
}

[[nodiscard]] source_identity identify_source(const struct stat& status) {
#if defined(__APPLE__)
   return {
       .object = identify_descriptor(status),
       .size = status.st_size,
       .modified_seconds = status.st_mtimespec.tv_sec,
       .modified_nanoseconds = status.st_mtimespec.tv_nsec,
   };
#else
   return {
       .object = identify_descriptor(status),
       .size = status.st_size,
       .modified_seconds = status.st_mtim.tv_sec,
       .modified_nanoseconds = status.st_mtim.tv_nsec,
   };
#endif
}

[[noreturn]] void throw_io(std::string_view operation) {
   FORGE_THROW_EXCEPTION(exceptions::io_failure, "projected workload token file operation failed",
                         forge::exceptions::ctx("operation", operation), forge::exceptions::ctx("error", errno));
}

[[nodiscard]] descriptor open_component(int directory, const std::string& name, int flags) {
   auto input = descriptor{::openat(directory, name.c_str(), flags)};
   if (input.get() >= 0) {
      return input;
   }

   const auto open_error = errno;
   struct stat status{};
   if (::fstatat(directory, name.c_str(), &status, AT_SYMLINK_NOFOLLOW) == 0 && S_ISLNK(status.st_mode)) {
      FORGE_THROW_EXCEPTION(exceptions::insecure_source,
                            "projected workload token path must not contain a symbolic link");
   }
   errno = open_error;
   throw_io("openat");
}

[[nodiscard]] bool entry_matches_descriptor(int directory, const std::string& name, const struct stat& expected,
                                            bool directory_expected) {
   struct stat current{};
   if (::fstatat(directory, name.c_str(), &current, AT_SYMLINK_NOFOLLOW) != 0) {
      if (errno == ENOENT) {
         return false;
      }
      throw_io("fstatat");
   }
   if (S_ISLNK(current.st_mode)) {
      FORGE_THROW_EXCEPTION(exceptions::insecure_source,
                            "projected workload token path must not contain a symbolic link");
   }
   if ((directory_expected && !S_ISDIR(current.st_mode)) || (!directory_expected && !S_ISREG(current.st_mode))) {
      FORGE_THROW_EXCEPTION(exceptions::insecure_source,
                            "projected workload token path component has an unexpected type");
   }
   return identify_descriptor(current) == identify_descriptor(expected);
}

struct descriptor_path {
   std::vector<descriptor> directories;
   std::vector<std::string> ancestor_names;
   std::string final_name;
};

[[nodiscard]] descriptor_path open_path(const std::filesystem::path& path) {
   auto result = descriptor_path{};
   auto flags = O_RDONLY | O_CLOEXEC | O_DIRECTORY;
#ifdef O_NOFOLLOW
   flags |= O_NOFOLLOW;
#else
   FORGE_THROW_EXCEPTION(exceptions::unavailable, "projected workload token files require O_NOFOLLOW support");
#endif

   const auto root = path.is_absolute() ? "/" : ".";
   result.directories.emplace_back(::open(root, flags));
   if (result.directories.back().get() < 0) {
      throw_io("open");
   }

   auto components = std::vector<std::string>{};
   const auto relative = path.is_absolute() ? path.relative_path() : path;
   for (const auto& component : relative) {
      auto name = component.native();
      if (name == ".") {
         continue;
      }
      if (name.empty() || name.find('\0') != std::string::npos || name == "..") {
         FORGE_THROW_EXCEPTION(exceptions::insecure_source,
                               "projected workload token path must name a contained regular file");
      }
      components.push_back(std::move(name));
   }
   if (components.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::insecure_source, "projected workload token source must name a regular file");
   }

   for (auto index = std::size_t{}; index + 1U < components.size(); ++index) {
      auto next = open_component(result.directories.back().get(), components[index], flags);
      struct stat status{};
      if (::fstat(next.get(), &status) != 0) {
         throw_io("fstat");
      }
      if (!S_ISDIR(status.st_mode)) {
         FORGE_THROW_EXCEPTION(exceptions::insecure_source,
                               "projected workload token path component must be a directory");
      }
      result.ancestor_names.push_back(components[index]);
      result.directories.push_back(std::move(next));
   }
   result.final_name = std::move(components.back());
   return result;
}

[[nodiscard]] std::optional<forge::crypto::core::secret_string> read_once(const projected_token_file_options& options) {
   auto flags = O_RDONLY | O_CLOEXEC;
#ifdef O_NOFOLLOW
   flags |= O_NOFOLLOW;
#else
   FORGE_THROW_EXCEPTION(exceptions::unavailable, "projected workload token files require O_NOFOLLOW support");
#endif
   auto path = open_path(options.path);
   const auto input = open_component(path.directories.back().get(), path.final_name, flags);

   struct stat before_status{};
   if (::fstat(input.get(), &before_status) != 0) {
      throw_io("fstat");
   }
   if (!S_ISREG(before_status.st_mode)) {
      FORGE_THROW_EXCEPTION(exceptions::insecure_source, "projected workload token source must be a regular file");
   }
   if (before_status.st_size < 0 || static_cast<std::uint64_t>(before_status.st_size) > options.max_bytes) {
      FORGE_THROW_EXCEPTION(exceptions::source_too_large,
                            "projected workload token source exceeds its configured size limit");
   }

   auto value = std::string{};
   try {
      value.resize(static_cast<std::size_t>(before_status.st_size));
      auto offset = std::size_t{};
      while (offset < value.size()) {
         const auto count = ::read(input.get(), value.data() + offset, value.size() - offset);
         if (count < 0 && errno == EINTR) {
            continue;
         }
         if (count < 0) {
            throw_io("read");
         }
         if (count == 0) {
            forge::crypto::core::secure_erase(value);
            return std::nullopt;
         }
         offset += static_cast<std::size_t>(count);
      }

      auto extra = char{};
      auto extra_count = ssize_t{};
      do {
         extra_count = ::read(input.get(), &extra, 1U);
      } while (extra_count < 0 && errno == EINTR);
      const auto extra_error = errno;
      forge::crypto::core::secure_erase(std::span<std::uint8_t>{reinterpret_cast<std::uint8_t*>(&extra), 1U});
      if (extra_count < 0) {
         errno = extra_error;
         throw_io("read");
      }

      struct stat after_status{};
      if (::fstat(input.get(), &after_status) != 0) {
         throw_io("fstat");
      }
      auto stable_path = entry_matches_descriptor(path.directories.back().get(), path.final_name, before_status, false);
      for (auto index = std::size_t{}; stable_path && index < path.ancestor_names.size(); ++index) {
         struct stat directory_status{};
         if (::fstat(path.directories[index + 1U].get(), &directory_status) != 0) {
            throw_io("fstat");
         }
         stable_path = entry_matches_descriptor(path.directories[index].get(), path.ancestor_names[index],
                                                directory_status, true);
      }
      if (extra_count != 0 || identify_source(before_status) != identify_source(after_status) || !stable_path) {
         forge::crypto::core::secure_erase(value);
         return std::nullopt;
      }
      if (value.empty()) {
         FORGE_THROW_EXCEPTION(exceptions::token_unavailable, "projected workload token file is empty");
      }
      return forge::crypto::core::secret_string{std::move(value)};
   } catch (...) {
      forge::crypto::core::secure_erase(value);
      throw;
   }
}

[[nodiscard]] forge::crypto::core::secret_string read_rotating(const projected_token_file_options& options) {
   for (auto attempt = std::size_t{}; attempt < options.max_rotation_attempts; ++attempt) {
      if (auto token = read_once(options)) {
         return std::move(*token);
      }
   }
   FORGE_THROW_EXCEPTION(exceptions::source_rotated, "projected workload token source changed while being read");
}
#else
[[nodiscard]] forge::crypto::core::secret_string read_rotating(const projected_token_file_options&) {
   FORGE_THROW_EXCEPTION(exceptions::unavailable, "projected workload token files are unavailable on this platform");
}
#endif

} // namespace

projected_token_file::projected_token_file(forge::asio::compute::executor read_executor,
                                           projected_token_file_options options)
    : read_executor_{std::move(read_executor)}, options_{std::move(options)} {
   validate_options(options_);
   if (!read_executor_.valid()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options, "projected workload token requires a valid read executor");
   }
}

boost::asio::awaitable<subject_token> projected_token_file::async_subject_token() {
   auto secret = co_await read_executor_.execute({.name = "projected workload token"},
                                                 [options = options_] { return read_rotating(options); });
   co_return subject_token{std::move(secret)};
}

} // namespace forge::auth::workload
