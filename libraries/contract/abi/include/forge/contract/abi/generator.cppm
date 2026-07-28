module;

#include <filesystem>
#include <string>
#include <vector>

export module forge.contract.abi.generator;

export namespace forge::contract::abi {

struct library_compilation {
   std::string owner;
   std::filesystem::path object_list;
};

struct request {
   std::string contract;
   std::filesystem::path abi;
   std::filesystem::path dispatcher;
   std::filesystem::path depfile;
   std::filesystem::path contract_graph;
   std::filesystem::path attribute_plugin;
   std::filesystem::path sysroot;
   std::filesystem::path ricardian_contracts;
   std::filesystem::path ricardian_clauses;
   std::vector<std::filesystem::path> module_paths;
   std::vector<std::filesystem::path> include_paths;
   std::vector<std::filesystem::path> sources;
   std::vector<std::filesystem::path> dependency_sources;
   std::vector<library_compilation> library_compilations;
   std::vector<std::filesystem::path> source_wrappers;
};

struct artifacts {
   std::filesystem::path abi;
   std::filesystem::path dispatcher;
   std::vector<std::filesystem::path> source_wrappers;
};

artifacts generate(const request& options);

} // namespace forge::contract::abi
