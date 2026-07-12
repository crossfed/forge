#pragma once

namespace forge::plugins::crypto::secrets {

inline void require_complete_file_read(std::istream& input, std::size_t expected_size,
                                       const std::string& path, const std::string& id) {
   const auto read_count = input.gcount();
   if (read_count < 0 || static_cast<std::size_t>(read_count) != expected_size || input.bad()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_source, "secret file read was incomplete",
                            forge::exceptions::ctx("secret_id", id),
                            forge::exceptions::ctx("path", path));
   }
}

} // namespace forge::plugins::crypto::secrets
