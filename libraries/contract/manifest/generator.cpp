module;

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

module forge.contract.manifest.generator;

import forge.codec.json;
import forge.contract.graph;
import forge.crypto.digest.sha256;
import forge.variant.value;
import forge.vm.wasm.backend;

namespace {

namespace wasm = forge::vm::wasm;

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

struct source_file {
   std::string owner;
   std::string role;
   std::string logical_path;
   std::string sha256;
};

struct dependency_edge {
   std::string owner;
   std::string kind;
   std::string dependency;
   std::string scope;
};

struct source_graph {
   std::vector<source_file> files;
   std::vector<dependency_edge> dependencies;
   std::string sha256;
};

void write_length(forge::crypto::digest::sha256::encoder& encoder, std::uint64_t value) {
   auto encoded = std::array<char, 8>{};
   for (auto index = std::size_t{}; index < encoded.size(); ++index) {
      encoded[encoded.size() - index - 1U] = static_cast<char>(value & 0xffU);
      value >>= 8U;
   }
   encoder.write(encoded.data(), static_cast<std::uint32_t>(encoded.size()));
}

void write_field(forge::crypto::digest::sha256::encoder& encoder, std::string_view value) {
   write_length(encoder, value.size());
   if (!value.empty()) {
      encoder.write(value.data(), static_cast<std::uint32_t>(value.size()));
   }
}

source_graph build_source_graph(const forge::contract::graph::descriptor& descriptor) {
   auto graph = source_graph{};
   graph.files.reserve(descriptor.files.size());
   for (const auto& file : descriptor.files) {
      graph.files.push_back(source_file{
          .owner = file.owner,
          .role = forge::contract::graph::to_string(file.role),
          .logical_path = file.logical_path.generic_string(),
          .sha256 = sha256(read_bytes(file.physical_path)),
      });
   }
   graph.dependencies.reserve(descriptor.dependencies.size());
   for (const auto& edge : descriptor.dependencies) {
      graph.dependencies.push_back(dependency_edge{
          .owner = edge.owner,
          .kind = forge::contract::graph::to_string(edge.kind),
          .dependency = edge.target,
          .scope = forge::contract::graph::to_string(edge.scope),
      });
   }

   std::ranges::sort(graph.files, {}, [](const auto& value) {
      return std::tie(value.owner, value.role, value.logical_path, value.sha256);
   });
   std::ranges::sort(graph.dependencies, {}, [](const auto& value) {
      return std::tie(value.owner, value.kind, value.dependency, value.scope);
   });

   auto encoder = forge::crypto::digest::sha256::encoder{};
   write_field(encoder, "forge.contract.source-graph.v2");
   write_length(encoder, graph.files.size());
   for (const auto& file : graph.files) {
      write_field(encoder, "file");
      write_field(encoder, file.owner);
      write_field(encoder, file.role);
      write_field(encoder, file.logical_path);
      write_field(encoder, file.sha256);
   }
   write_length(encoder, graph.dependencies.size());
   for (const auto& edge : graph.dependencies) {
      write_field(encoder, "dependency");
      write_field(encoder, edge.owner);
      write_field(encoder, edge.kind);
      write_field(encoder, edge.dependency);
      write_field(encoder, edge.scope);
   }
   graph.sha256 = encoder.result().str();
   return graph;
}

forge::variant source_graph_value(const source_graph& graph) {
   auto files = forge::variants{};
   files.reserve(graph.files.size());
   for (const auto& file : graph.files) {
      files.emplace_back(forge::mutable_variant_object{}("owner", file.owner)("role", file.role)(
          "logical_path", file.logical_path)("sha256", file.sha256));
   }
   auto dependencies = forge::variants{};
   dependencies.reserve(graph.dependencies.size());
   for (const auto& edge : graph.dependencies) {
      dependencies.emplace_back(forge::mutable_variant_object{}("owner", edge.owner)("kind", edge.kind)(
          "dependency", edge.dependency)("scope", edge.scope));
   }
   return forge::mutable_variant_object{}("files", std::move(files))("dependencies",
                                                                     std::move(dependencies))("sha256", graph.sha256);
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

namespace forge::contract::manifest {

void generate(const request& options) {
   const auto wasm_bytes = read_bytes(options.wasm);
   const auto abi_bytes = read_bytes(options.abi);
   const auto registry_bytes = read_bytes(options.imports);
   const auto graph = build_source_graph(forge::contract::graph::read(options.source_graph));

   auto root = forge::mutable_variant_object{};
   root("schema_version", std::uint64_t{2});
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
   root("source_graph", source_graph_value(graph));

   const auto encoded = forge::codec::json::write_value(forge::variant{std::move(root)}, {.pretty = true});
   if (!encoded.ok()) {
      throw std::runtime_error{"failed to encode contract manifest"};
   }
   write_text(options.output, encoded.text + '\n');
}

} // namespace forge::contract::manifest
