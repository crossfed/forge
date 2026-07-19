module;

#include <filesystem>
#include <string>

export module forge.contract.validation.validator;

export namespace forge::contract::validation {

struct request {
   std::filesystem::path wasm;
   std::filesystem::path abi;
   std::filesystem::path imports;
   std::string required_export = "apply";
};

void validate(const request& options);

} // namespace forge::contract::validation
