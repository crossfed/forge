#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

import forge.codec.json;
import forge.crypto.sha256;
import forge.variant.value;
import forge.vm.wasm.backend;

namespace {

namespace wasm = forge::vm::wasm;

struct arguments {
   std::map<std::string, std::string> values;
};

arguments parse(int argc, char** argv) {
   auto result = arguments{};
   for (auto index = 1; index < argc; index += 2) {
      if (index + 1 >= argc) {
         throw std::runtime_error{"manifest argument requires a value: " + std::string{argv[index]}};
      }
      auto name = std::string{argv[index]};
      if (!name.starts_with("--")) {
         throw std::runtime_error{"invalid manifest argument: " + name};
      }
      result.values.emplace(name.substr(2U), argv[index + 1]);
   }
   return result;
}

const std::string& require(const arguments& values, std::string_view name) {
   const auto found = values.values.find(std::string{name});
   if (found == values.values.end() || found->second.empty()) {
      throw std::runtime_error{"missing --" + std::string{name}};
   }
   return found->second;
}

std::vector<std::uint8_t> read_bytes(const std::filesystem::path& path) {
   auto input = std::ifstream{path, std::ios::binary | std::ios::ate};
   if (!input) {
      throw std::runtime_error{"cannot open artifact: " + path.string()};
   }
   const auto size = input.tellg();
   if (size < 0) {
      throw std::runtime_error{"cannot determine artifact size"};
   }
   auto result = std::vector<std::uint8_t>(static_cast<std::size_t>(size));
   input.seekg(0);
   if (!result.empty() && !input.read(reinterpret_cast<char*>(result.data()), size)) {
      throw std::runtime_error{"cannot read complete artifact"};
   }
   return result;
}

std::string text(const auto& bytes) {
   return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

std::string sha256(const std::vector<std::uint8_t>& bytes) {
   return forge::crypto::sha256::hash(std::span<const std::uint8_t>{bytes}).str();
}

forge::variants imported_functions(const std::vector<std::uint8_t>& bytes) {
   auto code = wasm::wasm_code{bytes.begin(), bytes.end()};
   using validator = wasm::backend<std::nullptr_t, wasm::null_backend, wasm::compatibility_options>;
   auto parsed = validator{code, static_cast<wasm::wasm_allocator*>(nullptr)};
   const auto& module = parsed.get_module();
   auto result = forge::variants{};
   for (std::size_t index = 0; index < module.imports.size(); ++index) {
      const auto& entry = module.imports[index];
      result.emplace_back(
          forge::mutable_variant_object{}("module", text(entry.module_str))("name", text(entry.field_str)));
   }
   return result;
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

void write_text(const std::filesystem::path& path, std::string_view value) {
   std::filesystem::create_directories(path.parent_path());
   auto output = std::ofstream{path, std::ios::binary | std::ios::trunc};
   if (!output || !(output << value)) {
      throw std::runtime_error{"cannot write contract manifest: " + path.string()};
   }
}

} // namespace

int main(int argc, char** argv) {
   try {
      const auto options = parse(argc, argv);
      const auto wasm_path = std::filesystem::path{require(options, "wasm")};
      const auto abi_path = std::filesystem::path{require(options, "abi")};
      const auto wasm_bytes = read_bytes(wasm_path);
      const auto abi_bytes = read_bytes(abi_path);
      const auto registry_bytes = read_bytes(require(options, "imports"));

      auto root = forge::mutable_variant_object{};
      root("schema_version", std::uint64_t{1});
      root("sdk",
           forge::mutable_variant_object{}("version", require(options, "sdk-version"))(
               "profile", require(options, "profile"))("reproducible", parse_bool(require(options, "reproducible"))));
      root("llvm", forge::mutable_variant_object{}("version", require(options, "llvm-version"))(
                       "commit", require(options, "llvm-commit")));
      root("sysroot", forge::mutable_variant_object{}("schema_version", require(options, "sysroot-version"))(
                          "sha256", require(options, "sysroot-hash")));
      root("intrinsics", forge::mutable_variant_object{}("interface_version", require(options, "intrinsic-version"))(
                             "sha256", sha256(registry_bytes)));
      root("wasm", forge::mutable_variant_object{}("sha256", sha256(wasm_bytes))(
                       "features", forge::variants{forge::variant{"mvp"}})("imports", imported_functions(wasm_bytes)));
      root("abi", forge::mutable_variant_object{}("sha256", sha256(abi_bytes)));

      const auto encoded = forge::codec::json::write_value(forge::variant{std::move(root)}, {.pretty = true});
      if (!encoded.ok()) {
         throw std::runtime_error{"failed to encode contract manifest"};
      }
      write_text(require(options, "output"), encoded.text + '\n');
      return 0;
   } catch (const std::exception& error) {
      std::cerr << "forge-contract-manifest: " << error.what() << '\n';
      return 1;
   }
}
