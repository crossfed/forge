module;

#include <cstddef>
#include <filesystem>
#include <string>

export module forge.crypto.keystore.password;

export import forge.crypto.core.secret_string;
export import forge.crypto.keystore.exceptions;

export namespace forge::crypto::keystore {

enum class password_source {
   terminal,
   standard_input,
   file,
};

struct password_request {
   password_source source = password_source::terminal;
   std::filesystem::path file;
   std::string prompt = "Password: ";
   std::size_t max_bytes = 4U * 1024U;
};

[[nodiscard]] core::secret_string read_password(password_request request = {});

} // namespace forge::crypto::keystore
