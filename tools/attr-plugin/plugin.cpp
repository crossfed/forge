#include <clang/AST/ASTConsumer.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendPluginRegistry.h>

#include <memory>
#include <string>
#include <vector>

import forge.tooling.abi.compilation_metadata;
import forge.tooling.attributes.registry;

namespace {

const auto registered = [] {
   forge::tooling::attributes::register_all();
   return true;
}();

class metadata_action final : public clang::PluginASTAction {
 public:
   bool ParseArgs(const clang::CompilerInstance&, const std::vector<std::string>& arguments) override {
      return arguments.empty();
   }

   ActionType getActionType() override {
      return AddBeforeMainAction;
   }

   std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance& compiler,
                                                         llvm::StringRef input_file) override {
      return forge::tooling::abi::create_compilation_metadata_consumer(compiler, input_file.str());
   }
};

clang::FrontendPluginRegistry::Add<metadata_action> metadata{
    "forge_contract_metadata",
    "Record contract dependencies from Clang compilation state",
};

} // namespace
