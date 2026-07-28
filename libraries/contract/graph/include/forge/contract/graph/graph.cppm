module;

#include <filesystem>
#include <string>
#include <vector>

export module forge.contract.graph;

export import forge.contract.graph.exceptions;

export namespace forge::contract::graph {

enum class file_role {
   dispatch_source,
   source,
   header,
   compile_check,
   ricardian_contracts,
   ricardian_clauses,
   module,
   implementation,
   public_header,
   private_header,
};

enum class dependency_kind {
   library,
   component,
};

enum class dependency_scope {
   public_,
   private_,
};

struct source_root {
   std::string owner;
   std::filesystem::path physical_path;
};

struct file {
   std::string owner;
   file_role role;
   std::filesystem::path logical_path;
   std::filesystem::path physical_path;
};

struct dependency {
   std::string owner;
   dependency_kind kind;
   std::string target;
   dependency_scope scope;
};

struct component {
   std::string id;
   std::vector<std::string> modules;
};

struct descriptor {
   std::string root_owner;
   std::vector<source_root> source_roots;
   std::vector<file> files;
   std::vector<dependency> dependencies;
   std::vector<component> components;
};

[[nodiscard]] descriptor read(const std::filesystem::path& path);
[[nodiscard]] bool is_public(file_role value);
[[nodiscard]] std::string to_string(file_role value);
[[nodiscard]] std::string to_string(dependency_kind value);
[[nodiscard]] std::string to_string(dependency_scope value);

} // namespace forge::contract::graph
