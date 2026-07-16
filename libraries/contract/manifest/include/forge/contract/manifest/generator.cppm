module;

#include <filesystem>
#include <string>

export module forge.contract.manifest.generator;

export namespace forge::contract::manifest {

struct request {
   std::filesystem::path wasm;
   std::filesystem::path abi;
   std::filesystem::path imports;
   std::filesystem::path output;
   std::string sdk_version;
   std::string profile;
   bool reproducible = false;
   std::string llvm_version;
   std::string llvm_commit;
   std::string sysroot_version;
   std::string sysroot_hash;
   std::string intrinsic_version;
};

void generate(const request& options);

} // namespace forge::contract::manifest
