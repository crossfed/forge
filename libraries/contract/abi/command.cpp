module;

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

module forge.contract.abi.command;

import forge.contract.abi.generator;

namespace forge::contract::abi::command {
namespace {

library_dependency_scope parse_library_dependency_scope(std::string_view scope) {
   if (scope == "PUBLIC") {
      return library_dependency_scope::public_;
   }
   if (scope == "PRIVATE") {
      return library_dependency_scope::private_;
   }
   throw std::runtime_error{"contract library dependency has an invalid scope: " + std::string{scope}};
}

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
      } else if (option == "--external-source-root") {
         result.external_source_roots.emplace_back(next());
      } else if (option == "--attested-source") {
         result.attested_sources.emplace_back(next());
      } else if (option == "--library-source") {
         auto owner = std::string{next()};
         const auto role = next();
         const auto parsed_role = [&] {
            if (role == "module") {
               return library_source_role::module;
            }
            if (role == "implementation") {
               return library_source_role::implementation;
            }
            if (role == "public_header") {
               return library_source_role::public_header;
            }
            if (role == "private_header") {
               return library_source_role::private_header;
            }
            throw std::runtime_error{"contract library source has an invalid role: " + std::string{role}};
         }();
         result.library_sources.push_back(library_source{
             .owner = std::move(owner),
             .role = parsed_role,
             .physical_path = next(),
         });
      } else if (option == "--library-translation-unit") {
         result.library_translation_units.push_back(library_translation_unit{
             .owner = std::string{next()},
             .physical_path = next(),
         });
      } else if (option == "--library-dependency") {
         auto owner = std::string{next()};
         auto dependency = std::string{next()};
         const auto scope = parse_library_dependency_scope(next());
         result.library_dependencies.push_back(library_dependency{
             .owner = std::move(owner),
             .dependency = std::move(dependency),
             .scope = scope,
         });
      } else if (option == "--library-external-module-source") {
         auto owner = std::string{next()};
         const auto scope = parse_library_dependency_scope(next());
         result.library_external_module_sources.push_back(library_external_module_source{
             .owner = std::move(owner),
             .scope = scope,
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
