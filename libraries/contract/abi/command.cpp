module;

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

module forge.contract.abi.command;

import forge.contract.abi.generator;

namespace forge::contract::abi::command {
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
      if (option == "--contract") {
         result.contract = next();
      } else if (option == "--abi") {
         result.abi = next();
      } else if (option == "--dispatch") {
         result.dispatcher = next();
      } else if (option == "--depfile") {
         result.depfile = next();
      } else if (option == "--source-dependencies") {
         result.source_dependencies = next();
      } else if (option == "--attribute-plugin") {
         result.attribute_plugin = next();
      } else if (option == "--sysroot") {
         result.sysroot = next();
      } else if (option == "--ricardian-contracts") {
         result.ricardian_contracts = next();
      } else if (option == "--ricardian-clauses") {
         result.ricardian_clauses = next();
      } else if (option == "--module-path") {
         result.module_paths.emplace_back(next());
      } else if (option == "--include") {
         result.include_paths.emplace_back(next());
      } else if (option == "--source-root") {
         result.source_roots.push_back(source_root{
             .logical_path = std::string{next()},
             .physical_path = next(),
         });
      } else if (option == "--dependency-source") {
         result.dependency_sources.emplace_back(next());
      } else if (option == "--source-wrapper") {
         result.source_wrappers.emplace_back(next());
      } else if (option.starts_with("--")) {
         throw std::runtime_error{"unknown argument: " + std::string{option}};
      } else {
         result.sources.emplace_back(option);
      }
   }
   if (result.contract.empty() || result.abi.empty() || result.dispatcher.empty() || result.attribute_plugin.empty() ||
       result.sysroot.empty() || result.sources.empty()) {
      throw std::runtime_error{
          "--contract, --abi, --dispatch, --attribute-plugin, --sysroot and contract sources are required"};
   }
   if (!result.source_wrappers.empty() && result.source_wrappers.size() + 1U != result.sources.size()) {
      throw std::runtime_error{"--source-wrapper must be specified once for every non-dispatch source"};
   }
   return result;
}

} // namespace

int run(int argc, const char* const* argv) {
   try {
      generate(parse(argc, argv));
      return 0;
   } catch (const std::exception& error) {
      std::cerr << "abigen: " << error.what() << '\n';
      return 1;
   }
}

} // namespace forge::contract::abi::command
