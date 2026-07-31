module;

#include <charconv>
#include <cstdint>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

module forge.contract.manifest.command;

import forge.contract.manifest.generator;

namespace forge::contract::manifest::command {
namespace {

std::map<std::string, std::string> parse(int argc, const char* const* argv) {
   auto result = std::map<std::string, std::string>{};
   for (auto index = 1; index < argc; index += 2) {
      if (index + 1 >= argc) {
         throw std::runtime_error{"manifest argument requires a value: " + std::string{argv[index]}};
      }
      auto name = std::string{argv[index]};
      if (!name.starts_with("--")) {
         throw std::runtime_error{"invalid manifest argument: " + name};
      }
      if (!result.emplace(name.substr(2U), argv[index + 1]).second) {
         throw std::runtime_error{"duplicate manifest argument: " + name};
      }
   }
   return result;
}

const std::string& require(const std::map<std::string, std::string>& values, std::string_view name) {
   const auto found = values.find(std::string{name});
   if (found == values.end() || found->second.empty()) {
      throw std::runtime_error{"missing --" + std::string{name}};
   }
   return found->second;
}

std::string optional(const std::map<std::string, std::string>& values, std::string_view name) {
   const auto found = values.find(std::string{name});
   return found == values.end() ? std::string{} : found->second;
}

bool parse_bool(std::string_view value) {
   if (value == "true" || value == "1" || value == "ON") {
      return true;
   }
   if (value == "false" || value == "0" || value == "OFF") {
      return false;
   }
   throw std::runtime_error{"invalid boolean value: " + std::string{value}};
}

std::uint64_t parse_version(std::string_view value) {
   auto result = std::uint64_t{};
   const auto* begin = value.data();
   const auto* end = begin + value.size();
   const auto [position, error] = std::from_chars(begin, end, result);
   if (error != std::errc{} || position != end) {
      throw std::runtime_error{"invalid numeric version: " + std::string{value}};
   }
   return result;
}

} // namespace

int run(int argc, const char* const* argv) {
   try {
      const auto values = parse(argc, argv);
      generate(request{
          .wasm = require(values, "wasm"),
          .abi = require(values, "abi"),
          .imports = require(values, "imports"),
          .output = require(values, "output"),
          .sdk_version = require(values, "sdk-version"),
          .profile = require(values, "profile"),
          .reproducible = parse_bool(require(values, "reproducible")),
          .llvm_version = require(values, "llvm-version"),
          .llvm_commit = optional(values, "llvm-commit"),
          .sysroot_version = parse_version(require(values, "sysroot-version")),
          .sysroot_hash = require(values, "sysroot-hash"),
          .intrinsic_version = parse_version(require(values, "intrinsic-version")),
      });
      return 0;
   } catch (const std::exception& error) {
      std::cerr << "contract-manifest: " << error.what() << '\n';
      return 1;
   }
}

} // namespace forge::contract::manifest::command
