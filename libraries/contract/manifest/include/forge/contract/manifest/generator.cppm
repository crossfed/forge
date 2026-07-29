module;

#include <cstdint>
#include <filesystem>
#include <string>

export module forge.contract.manifest.generator;

export namespace forge::contract::manifest {

struct request {
   std::filesystem::path wasm;
   std::filesystem::path abi;
   std::filesystem::path imports;
   std::filesystem::path source_graph;
   std::filesystem::path output;
   std::string sdk_version;
   std::string profile;
   bool reproducible = false;
   std::string llvm_version;
   std::string llvm_commit;
   std::uint64_t sysroot_version = 0;
   std::string sysroot_hash;
   std::uint64_t intrinsic_version = 0;
};

void generate(const request& options);

} // namespace forge::contract::manifest
