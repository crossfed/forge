module;

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

module forge.contract.manifest.generator;

import forge.codec.json;
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
   std::string dependency;
   std::string scope;
};

struct source_graph {
   std::vector<source_file> files;
   std::vector<dependency_edge> dependencies;
   std::string sha256;
};

std::vector<std::string> split(std::string_view value, char separator) {
   auto result = std::vector<std::string>{};
   while (true) {
      const auto position = value.find(separator);
      result.emplace_back(value.substr(0, position));
      if (position == std::string_view::npos) {
         return result;
      }
      value.remove_prefix(position + 1U);
   }
}

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

void append_source_file(source_graph& graph, std::set<std::pair<std::string, std::string>>& file_keys,
                        std::set<std::filesystem::path>& physical_paths, const std::vector<std::string>& fields,
                        bool skip_attested_physical_path) {
   if (fields.size() != 5U || fields[0] != "F" || fields[1].empty() || fields[2].empty() || fields[3].empty() ||
       fields[4].empty()) {
      throw std::runtime_error{"contract source graph contains an incomplete file record"};
   }
   const auto physical = std::filesystem::weakly_canonical(fields[4]);
   if (skip_attested_physical_path && physical_paths.contains(physical)) {
      return;
   }
   if (!file_keys.emplace(fields[1], fields[3]).second) {
      throw std::runtime_error{"contract source graph contains a duplicate logical path"};
   }
   const auto bytes = read_bytes(physical);
   physical_paths.insert(physical);
   graph.files.push_back(source_file{
       .owner = fields[1],
       .role = fields[2],
       .logical_path = fields[3],
       .sha256 = sha256(bytes),
   });
}

void read_source_dependencies(const std::filesystem::path& path, source_graph& graph,
                              std::set<std::pair<std::string, std::string>>& file_keys,
                              std::set<std::filesystem::path>& physical_paths) {
   auto input = std::ifstream{path, std::ios::binary};
   if (!input) {
      throw std::runtime_error{"cannot open contract source dependencies: " + path.string()};
   }

   auto header = std::string{};
   if (!std::getline(input, header) || header != "FORGE_CONTRACT_SOURCE_DEPENDENCIES_V1") {
      throw std::runtime_error{"contract source dependencies have an unsupported schema"};
   }
   auto line = std::string{};
   while (std::getline(input, line)) {
      if (!line.empty()) {
         append_source_file(graph, file_keys, physical_paths, split(line, '|'), true);
      }
   }
   if (!input.eof()) {
      throw std::runtime_error{"cannot read complete contract source dependencies"};
   }
}

source_graph read_source_graph(const std::filesystem::path& path, const std::filesystem::path& dependencies_path) {
   auto input = std::ifstream{path, std::ios::binary};
   if (!input) {
      throw std::runtime_error{"cannot open contract source graph: " + path.string()};
   }

   auto header = std::string{};
   if (!std::getline(input, header) || header != "FORGE_CONTRACT_SOURCE_GRAPH_V2") {
      throw std::runtime_error{"contract source graph has an unsupported schema"};
   }

   auto result = source_graph{};
   auto file_keys = std::set<std::pair<std::string, std::string>>{};
   auto physical_paths = std::set<std::filesystem::path>{};
   auto edge_keys = std::set<std::pair<std::string, std::string>>{};
   auto line = std::string{};
   while (std::getline(input, line)) {
      if (line.empty()) {
         continue;
      }
      const auto fields = split(line, '|');
      if (fields.size() == 5U && fields[0] == "F") {
         append_source_file(result, file_keys, physical_paths, fields, false);
      } else if (fields.size() == 4U && fields[0] == "E") {
         if (fields[1].empty() || fields[2].empty()) {
            throw std::runtime_error{"contract source graph contains an incomplete dependency edge"};
         }
         if (fields[3] != "PUBLIC" && fields[3] != "PRIVATE") {
            throw std::runtime_error{"contract source graph contains an invalid dependency scope"};
         }
         if (!edge_keys.emplace(fields[1], fields[2]).second) {
            throw std::runtime_error{"contract source graph contains a duplicate dependency edge"};
         }
         result.dependencies.push_back(
             dependency_edge{.owner = fields[1], .dependency = fields[2], .scope = fields[3]});
      } else {
         throw std::runtime_error{"contract source graph contains an invalid record"};
      }
   }
   if (!input.eof()) {
      throw std::runtime_error{"cannot read complete contract source graph"};
   }
   read_source_dependencies(dependencies_path, result, file_keys, physical_paths);
   if (result.files.empty()) {
      throw std::runtime_error{"contract source graph contains no files"};
   }

   auto owners = std::set<std::string>{};
   for (const auto& file : result.files) {
      owners.insert(file.owner);
   }
   for (const auto& edge : result.dependencies) {
      if (!owners.contains(edge.owner)) {
         throw std::runtime_error{"contract source graph dependency owner has no files: " + edge.owner};
      }
      if (!owners.contains(edge.dependency)) {
         throw std::runtime_error{"contract source graph dependency target has no files: " + edge.dependency};
      }
   }

   std::ranges::sort(result.files, {}, [](const auto& value) {
      return std::tie(value.owner, value.role, value.logical_path, value.sha256);
   });
   std::ranges::sort(result.dependencies, {},
                     [](const auto& value) { return std::tie(value.owner, value.dependency, value.scope); });

   auto encoder = forge::crypto::digest::sha256::encoder{};
   write_field(encoder, "forge.contract.source-graph.v2");
   write_length(encoder, result.files.size());
   for (const auto& file : result.files) {
      write_field(encoder, "file");
      write_field(encoder, file.owner);
      write_field(encoder, file.role);
      write_field(encoder, file.logical_path);
      write_field(encoder, file.sha256);
   }
   write_length(encoder, result.dependencies.size());
   for (const auto& edge : result.dependencies) {
      write_field(encoder, "dependency");
      write_field(encoder, edge.owner);
      write_field(encoder, edge.dependency);
      write_field(encoder, edge.scope);
   }
   result.sha256 = encoder.result().str();
   return result;
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
      dependencies.emplace_back(
          forge::mutable_variant_object{}("owner", edge.owner)("dependency", edge.dependency)("scope", edge.scope));
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
   const auto graph = read_source_graph(options.source_graph, options.source_dependencies);

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
