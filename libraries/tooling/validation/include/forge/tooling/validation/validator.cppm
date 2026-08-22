module;

#include <filesystem>
#include <string>

export module forge.tooling.validation.validator;

export namespace forge::tooling::validation {

struct request {
   std::filesystem::path wasm;
   std::filesystem::path abi;
   std::filesystem::path imports;
   std::string required_export = "apply";
};

void validate(const request& options);

} // namespace forge::tooling::validation
