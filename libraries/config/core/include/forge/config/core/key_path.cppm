module;

#include <string>
#include <vector>

export module forge.config.core.key_path;

export namespace forge::config::core {

struct key_path {
   std::string value;

   [[nodiscard]] std::vector<std::string> segments() const;
};

} // namespace forge::config::core
