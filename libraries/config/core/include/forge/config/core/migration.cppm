module;

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

export module forge.config.core.migration;

import forge.config.core.document;
import forge.schema.diagnostic;

export namespace forge::config::core {

struct migration_step {
   std::uint32_t from_version = 0;
   std::uint32_t to_version = 0;
   std::string description;
   std::function<void(document&)> apply;
};

class migration_plan {
 public:
   migration_plan& step(std::uint32_t from_version, std::uint32_t to_version, std::string description,
                        std::function<void(document&)> apply);

   [[nodiscard]] const std::vector<migration_step>& steps() const noexcept;
   [[nodiscard]] std::uint32_t target_version() const noexcept;

 private:
   std::vector<migration_step> steps_;
};

struct migration_options {
   std::string version_key = "version";
   std::uint32_t default_version = 0;
};

struct migration_result {
   document value;
   std::uint32_t from_version = 0;
   std::uint32_t to_version = 0;
   std::vector<schema::diagnostic> diagnostics;

   [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] migration_result migrate(document input, const migration_plan& plan,
                                       migration_options options = {});

} // namespace forge::config::core
