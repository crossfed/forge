#include <clang/AST/ASTConsumer.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/Module.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendPluginRegistry.h>
#include <clang/Lex/HeaderSearch.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Support/JSON.h>
#include <llvm/Support/raw_ostream.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

import forge.contract.attributes.registry;

namespace {

const auto registered = [] {
   forge::contract::attributes::register_all();
   return true;
}();

using source_dependencies = std::map<std::filesystem::path, bool>;
using module_dependencies = std::set<std::string>;

source_dependencies collect_source_dependencies(clang::CompilerInstance& compiler) {
   const auto& source_manager = compiler.getSourceManager();
   auto& header_search = compiler.getPreprocessor().getHeaderSearchInfo();
   auto result = source_dependencies{};
   for (auto iterator = source_manager.fileinfo_begin(); iterator != source_manager.fileinfo_end(); ++iterator) {
      const auto path = std::filesystem::path{iterator->first.getName().str()};
      if (path.empty() || !std::filesystem::exists(path)) {
         continue;
      }
      const auto is_system = clang::SrcMgr::isSystem(header_search.getFileDirFlavor(iterator->first));
      const auto canonical = std::filesystem::weakly_canonical(path);
      const auto [entry, inserted] = result.emplace(canonical, is_system);
      if (!inserted) {
         entry->second = entry->second && is_system;
      }
   }
   return result;
}

void collect_module_dependencies(clang::ASTContext& context, module_dependencies& imported,
                                 module_dependencies& exported, module_dependencies& provided) {
   for (const auto* declaration : context.local_imports()) {
      if (const auto* module = declaration->getImportedModule(); module != nullptr) {
         imported.insert(module->getFullModuleName());
      }
   }
   if (const auto* current = context.getCurrentNamedModule(); current != nullptr) {
      provided.insert(current->getFullModuleName());
      auto modules = llvm::SmallVector<clang::Module*, 4>{};
      current->getExportedModules(modules);
      for (const auto* module : modules) {
         exported.insert(module->getFullModuleName());
      }
   }
}

llvm::json::Array module_array(const module_dependencies& modules) {
   auto result = llvm::json::Array{};
   result.reserve(modules.size());
   for (const auto& module : modules) {
      result.emplace_back(module);
   }
   return result;
}

class metadata_consumer final : public clang::ASTConsumer {
 public:
   metadata_consumer(clang::CompilerInstance& compiler, std::filesystem::path input)
       : compiler_{compiler}, input_{std::move(input)} {}

   void HandleTranslationUnit(clang::ASTContext& context) override {
      const auto& output = compiler_.getFrontendOpts().OutputFile;
      if (output.empty() || output == "-") {
         return;
      }

      auto imported = module_dependencies{};
      auto exported = module_dependencies{};
      auto provided = module_dependencies{};
      collect_module_dependencies(context, imported, exported, provided);

      auto dependencies = llvm::json::Array{};
      for (const auto& [path, system] : collect_source_dependencies(compiler_)) {
         dependencies.emplace_back(llvm::json::Object{
             {"path", path.generic_string()},
             {"system", system},
         });
      }

      auto document = llvm::json::Object{
          {"version", 1},
          {"source", std::filesystem::weakly_canonical(input_).generic_string()},
          {"dependencies", std::move(dependencies)},
          {"imports", module_array(imported)},
          {"exports", module_array(exported)},
          {"provides", module_array(provided)},
      };
      auto path = std::filesystem::absolute(output);
      path += ".forge-contract-metadata.json";
      auto temporary = path;
      temporary += ".tmp";

      auto stream = std::ofstream{temporary, std::ios::binary | std::ios::trunc};
      if (!stream) {
         report("cannot create Forge contract compilation metadata: " + temporary.string());
         return;
      }
      auto encoded = std::string{};
      auto output_stream = llvm::raw_string_ostream{encoded};
      output_stream << llvm::formatv("{0:2}", llvm::json::Value(std::move(document))) << '\n';
      output_stream.flush();
      stream << encoded;
      stream.close();
      if (!stream) {
         report("cannot write Forge contract compilation metadata: " + temporary.string());
         return;
      }

      auto error = std::error_code{};
      std::filesystem::rename(temporary, path, error);
      if (error) {
         std::filesystem::remove(path, error);
         error.clear();
         std::filesystem::rename(temporary, path, error);
      }
      if (error) {
         report("cannot publish Forge contract compilation metadata: " + error.message());
      }
   }

 private:
   void report(const std::string& message) {
      const auto diagnostic = compiler_.getDiagnostics().getCustomDiagID(clang::DiagnosticsEngine::Error, "%0");
      compiler_.getDiagnostics().Report(diagnostic) << message;
   }

   clang::CompilerInstance& compiler_;
   std::filesystem::path input_;
};

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
      return std::make_unique<metadata_consumer>(compiler, std::filesystem::path{input_file.str()});
   }
};

clang::FrontendPluginRegistry::Add<metadata_action> metadata{
    "forge_contract_metadata",
    "Record contract source dependencies from the compiler AST",
};

} // namespace
