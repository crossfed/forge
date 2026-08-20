module;

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

module forge.tooling.validation.command;

import forge.tooling.validation.validator;

namespace forge::tooling::validation::command {
namespace {

request parse(int argc, const char* const* argv) {
   auto result = request{};
   for (auto index = 1; index < argc; ++index) {
      const auto option = std::string_view{argv[index]};
      const auto next = [&]() -> std::string_view {
         if (++index >= argc) {
            throw std::runtime_error{"missing value after " + std::string{option}};
         }
         return argv[index];
      };
      if (option == "--wasm") {
         result.wasm = next();
      } else if (option == "--abi") {
         result.abi = next();
      } else if (option == "--imports") {
         result.imports = next();
      } else if (option == "--required-export") {
         result.required_export = next();
      } else {
         throw std::runtime_error{"unknown argument: " + std::string{option}};
      }
   }
   if (result.wasm.empty() || result.abi.empty() || result.imports.empty()) {
      throw std::runtime_error{"--wasm, --abi and --imports are required"};
   }
   return result;
}

} // namespace

int run(int argc, const char* const* argv) {
   try {
      validate(parse(argc, argv));
      return 0;
   } catch (const std::exception& error) {
      std::cerr << "contract-check: " << error.what() << '\n';
      return 1;
   }
}

} // namespace forge::tooling::validation::command
