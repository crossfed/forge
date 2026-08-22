module;

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/ASTContext.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/Module.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Frontend/CompilerInstance.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Support/JSON.h>
#include <llvm/Support/raw_ostream.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

module forge.tooling.abi.compilation_metadata;

namespace forge::tooling::abi {
namespace {

using source_dependencies = std::map<std::filesystem::path, bool>;
using module_dependencies = std::set<std::string>;

source_dependencies collect_source_dependencies(clang::CompilerInstance& compiler) {
   const auto& source_manager = compiler.getSourceManager();

   // SourceManager owns a mutable container. Copy stable file handles before
   // path normalization so iteration cannot be invalidated by Clang lookups.
   auto files = std::vector<clang::FileEntryRef>{};
   files.reserve(std::distance(source_manager.fileinfo_begin(), source_manager.fileinfo_end()));
   for (auto iterator = source_manager.fileinfo_begin(); iterator != source_manager.fileinfo_end(); ++iterator) {
      files.push_back(iterator->first);
   }

   auto result = source_dependencies{};
   for (const auto& file : files) {
      const auto path = std::filesystem::path{file.getName().str()};
      if (path.empty() || !std::filesystem::exists(path)) {
         continue;
      }
      const auto file_id = source_manager.translateFile(file);
      const auto system =
          file_id.isValid() && source_manager.isInSystemHeader(source_manager.getLocForStartOfFile(file_id));
      const auto canonical = std::filesystem::weakly_canonical(path);
      const auto [entry, inserted] = result.emplace(canonical, system);
      if (!inserted) {
         entry->second = entry->second && system;
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

std::error_code publish_metadata(const std::filesystem::path& temporary, const std::filesystem::path& destination) {
#if defined(_WIN32)
   if (::MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) {
      return {static_cast<int>(::GetLastError()), std::system_category()};
   }
   return {};
#else
   auto error = std::error_code{};
   std::filesystem::rename(temporary, destination, error);
   return error;
#endif
}

class compilation_metadata_consumer final : public clang::ASTConsumer {
 public:
   compilation_metadata_consumer(clang::CompilerInstance& compiler, std::filesystem::path input)
       : compiler_{compiler}, input_{std::move(input)} {}

   void HandleTranslationUnit(clang::ASTContext& context) override {
      const auto& output_file = compiler_.getFrontendOpts().OutputFile;
      if (output_file.empty() || output_file == "-") {
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

      auto path = std::filesystem::absolute(output_file);
      path += ".forge-contract-metadata.json";
      auto temporary = path;
      temporary += ".tmp";

      auto stream = std::ofstream{temporary, std::ios::binary | std::ios::trunc};
      if (!stream) {
         report("cannot create Forge contract compilation metadata: " + temporary.string());
         return;
      }
      auto encoded = std::string{};
      auto output = llvm::raw_string_ostream{encoded};
      output << llvm::formatv("{0:2}", llvm::json::Value(std::move(document))) << '\n';
      output.flush();
      stream << encoded;
      stream.close();
      if (!stream) {
         report("cannot write Forge contract compilation metadata: " + temporary.string());
         return;
      }

      const auto error = publish_metadata(temporary, path);
      if (error) {
         auto ignored = std::error_code{};
         std::filesystem::remove(temporary, ignored);
         report("cannot publish Forge contract compilation metadata: " + error.message());
      }
   }

 private:
   void report(const std::string& message) {
      const auto id = compiler_.getDiagnostics().getCustomDiagID(clang::DiagnosticsEngine::Error, "%0");
      compiler_.getDiagnostics().Report(id) << message;
   }

   clang::CompilerInstance& compiler_;
   std::filesystem::path input_;
};

} // namespace

std::unique_ptr<clang::ASTConsumer> create_compilation_metadata_consumer(clang::CompilerInstance& compiler,
                                                                         std::string_view input_file) {
   return std::make_unique<compilation_metadata_consumer>(compiler, std::filesystem::path{input_file});
}

} // namespace forge::tooling::abi
