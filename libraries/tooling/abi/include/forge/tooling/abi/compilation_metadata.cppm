module;

#include <clang/AST/ASTConsumer.h>
#include <clang/Frontend/CompilerInstance.h>

#include <memory>
#include <string_view>

export module forge.tooling.abi.compilation_metadata;

export namespace forge::tooling::abi {

[[nodiscard]] std::unique_ptr<clang::ASTConsumer>
create_compilation_metadata_consumer(clang::CompilerInstance& compiler, std::string_view input_file);

} // namespace forge::tooling::abi
