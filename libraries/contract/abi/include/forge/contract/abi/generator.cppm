module;

#include <filesystem>
#include <string>
#include <vector>

export module forge.contract.abi.generator;

export namespace forge::contract::abi {

struct source_root {
   std::string logical_path;
   std::filesystem::path physical_path;
};

struct library_translation_unit {
   std::string owner;
   std::filesystem::path physical_path;
};

struct library_source {
   std::string owner;
   std::filesystem::path physical_path;
};

enum class library_dependency_scope {
   public_,
   private_,
};

struct library_dependency {
   std::string owner;
   std::string dependency;
   library_dependency_scope scope;
};

struct request {
   std::string contract;
   std::filesystem::path abi;
   std::filesystem::path dispatcher;
   std::filesystem::path depfile;
   std::filesystem::path source_dependencies;
   std::filesystem::path attribute_plugin;
   std::filesystem::path sysroot;
   std::filesystem::path ricardian_contracts;
   std::filesystem::path ricardian_clauses;
   std::vector<std::filesystem::path> module_paths;
   std::vector<std::filesystem::path> include_paths;
   std::vector<source_root> source_roots;
   std::vector<std::filesystem::path> external_source_roots;
   std::vector<std::filesystem::path> attested_sources;
   std::vector<library_source> library_sources;
   std::vector<library_translation_unit> library_translation_units;
   std::vector<library_dependency> library_dependencies;
   std::vector<std::filesystem::path> sources;
   std::vector<std::filesystem::path> dependency_sources;
   std::vector<std::filesystem::path> source_wrappers;
};

struct artifacts {
   std::filesystem::path abi;
   std::filesystem::path dispatcher;
   std::vector<std::filesystem::path> source_wrappers;
};

artifacts generate(const request& options);

} // namespace forge::contract::abi
