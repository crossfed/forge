module;

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

module forge.tooling.manifest.generator;

import forge.codec.json;
import forge.crypto.digest.sha256;
import forge.variant.value;
import forge.vm.wasm.interpret.backend;

namespace {

namespace wasm = forge::vm::wasm::interpret;

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
   return forge::crypto::digest::sha256::hash(std::span<const std::uint8_t>{bytes}).str();
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

void write_text(const std::filesystem::path& path, std::string_view value) {
   if (const auto parent = path.parent_path(); !parent.empty()) {
      std::filesystem::create_directories(parent);
   }
   auto output = std::ofstream{path, std::ios::binary | std::ios::trunc};
   if (!output || !(output << value)) {
      throw std::runtime_error{"cannot write contract manifest: " + path.string()};
   }
}

} // namespace

namespace forge::tooling::manifest {

void generate(const request& options) {
   const auto wasm_bytes = read_bytes(options.wasm);
   const auto abi_bytes = read_bytes(options.abi);
   const auto registry_bytes = read_bytes(options.imports);
   auto root = forge::mutable_variant_object{};
   root("schema_version", std::uint64_t{3});
   root("sdk", forge::mutable_variant_object{}("version", options.sdk_version)("profile", options.profile)(
                   "reproducible", options.reproducible));
   auto llvm = forge::mutable_variant_object{}("version", options.llvm_version);
   if (!options.llvm_commit.empty()) {
      llvm("commit", options.llvm_commit);
   }
   root("llvm", std::move(llvm));
   root("sysroot",
        forge::mutable_variant_object{}("schema_version", options.sysroot_version)("sha256", options.sysroot_hash));
   root("intrinsics", forge::mutable_variant_object{}("interface_version",
                                                      options.intrinsic_version)("sha256", sha256(registry_bytes)));
   root("wasm", forge::mutable_variant_object{}("sha256", sha256(wasm_bytes))(
                    "features", forge::variants{forge::variant{"mvp"}})("imports", imported_functions(wasm_bytes)));
   root("abi", forge::mutable_variant_object{}("sha256", sha256(abi_bytes)));

   const auto encoded = forge::codec::json::write_value(forge::variant{std::move(root)}, {.pretty = true});
   if (!encoded.ok()) {
      throw std::runtime_error{"failed to encode contract manifest"};
   }
   write_text(options.output, encoded.text + '\n');
}

} // namespace forge::tooling::manifest
