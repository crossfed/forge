module;

#include <clang/AST/Attr.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/Expr.h>
#include <clang/Basic/ParsedAttrInfo.h>
#include <clang/Sema/ParsedAttr.h>
#include <clang/Sema/Sema.h>

#include <array>
#include <string>
#include <string_view>

module forge.contract.attributes.registry;

namespace {

using clang::AttributeCommonInfo;
using clang::ParsedAttr;
using clang::ParsedAttrInfo;

std::string attribute_argument(clang::Sema& sema, const ParsedAttr& attribute) {
   if (attribute.getNumArgs() == 0U) {
      return {};
   }

   const auto* expression = attribute.getArgAsExpr(0)->IgnoreParenImpCasts();
   const auto* literal = llvm::dyn_cast<clang::StringLiteral>(expression);
   if (literal == nullptr) {
      const auto diagnostic = sema.getDiagnostics().getCustomDiagID(
          clang::DiagnosticsEngine::Error, "Forge contract attribute argument must be a string literal");
      sema.Diag(attribute.getLoc(), diagnostic);
      return {};
   }
   return literal->getString().str();
}

template <typename Declaration>
bool require_declaration(clang::Sema& sema, const ParsedAttr& attribute, clang::Decl* declaration,
                         const char* message) {
   if (llvm::isa<Declaration>(declaration)) {
      return true;
   }
   const auto diagnostic = sema.getDiagnostics().getCustomDiagID(clang::DiagnosticsEngine::Error, "%0");
   sema.Diag(attribute.getLoc(), diagnostic) << message;
   return false;
}

bool require_scope(clang::Sema& sema, const ParsedAttr& attribute) {
   if (const auto* scope = attribute.getScopeName(); scope != nullptr) {
      const auto name = scope->getName();
      if (name == "forge" || name == "eosio") {
         return true;
      }
   } else if (const auto* name = attribute.getAttrName(); name != nullptr) {
      const auto spelling = name->getName();
      if (spelling.starts_with("forge_") || spelling.starts_with("eosio_")) {
         return true;
      }
   }
   const auto diagnostic = sema.getDiagnostics().getCustomDiagID(
       clang::DiagnosticsEngine::Error, "Forge contract attributes require the 'forge' or 'eosio' namespace");
   sema.Diag(attribute.getLoc(), diagnostic);
   return false;
}

void preserve_attribute_scope(clang::Sema& sema, clang::Decl* declaration, const ParsedAttr& attribute,
                              std::string_view kind) {
   auto annotation = std::string{"forge.attribute_scope:"};
   annotation += kind;
   annotation += ':';
   if (const auto* scope = attribute.getScopeName(); scope != nullptr) {
      annotation += scope->getName();
   } else if (attribute.getAttrName()->getName().starts_with("eosio_")) {
      annotation += "eosio";
   } else {
      annotation += "forge";
   }
   declaration->addAttr(clang::AnnotateAttr::Create(sema.Context, annotation, nullptr, 0, attribute));
}

enum class declaration_kind {
   record,
   method,
   function,
   record_or_method,
};

class canonical_attribute : public ParsedAttrInfo {
 public:
   canonical_attribute(std::string_view kind, declaration_kind target, bool argument)
       : kind_{kind}, target_{target}, argument_{argument} {}

   bool diagAppertainsToDecl(clang::Sema& sema, const ParsedAttr& attribute,
                             const clang::Decl* declaration) const override {
      if (!require_scope(sema, attribute)) {
         return false;
      }
      const auto valid = [this, declaration] {
         switch (target_) {
         case declaration_kind::record:
            return llvm::isa<clang::RecordDecl>(declaration);
         case declaration_kind::method:
            return llvm::isa<clang::CXXMethodDecl>(declaration);
         case declaration_kind::function:
            return llvm::isa<clang::FunctionDecl>(declaration);
         case declaration_kind::record_or_method:
            return llvm::isa<clang::RecordDecl>(declaration) || llvm::isa<clang::CXXMethodDecl>(declaration);
         }
         return false;
      }();
      if (!valid) {
         const auto diagnostic = sema.getDiagnostics().getCustomDiagID(
             clang::DiagnosticsEngine::Error, "Forge contract attribute applies to an incompatible declaration");
         sema.Diag(attribute.getLoc(), diagnostic);
         return false;
      }
      if ((kind_ == "action" || kind_ == "call" || kind_ == "on_notify") &&
          llvm::isa<clang::CXXMethodDecl>(declaration) && llvm::cast<clang::CXXMethodDecl>(declaration)->isStatic()) {
         const auto diagnostic = sema.getDiagnostics().getCustomDiagID(
             clang::DiagnosticsEngine::Error, "contract entry point attribute applies only to a non-static method");
         sema.Diag(attribute.getLoc(), diagnostic);
         return false;
      }
      return true;
   }

   AttrHandling handleDeclAttribute(clang::Sema& sema, clang::Decl* declaration,
                                    const ParsedAttr& attribute) const override {
      auto annotation = std::string{"forge."} + kind_;
      if (argument_ && attribute.getNumArgs() != 0U) {
         annotation += ':';
         annotation += attribute_argument(sema, attribute);
      }
      declaration->addAttr(clang::AnnotateAttr::Create(sema.Context, annotation, nullptr, 0, attribute));
      preserve_attribute_scope(sema, declaration, attribute, kind_);
      return AttributeApplied;
   }

 private:
   std::string kind_;
   declaration_kind target_;
   bool argument_ = false;
};

class contract_attribute final : public ParsedAttrInfo {
 public:
   contract_attribute() {
      static constexpr auto spellings = std::array{
          // Clang probes the unqualified basename before parsing arguments of a scoped plugin attribute.
          Spelling{AttributeCommonInfo::AS_CXX11, "contract"},
          Spelling{AttributeCommonInfo::AS_CXX11, "forge::contract"},
          Spelling{AttributeCommonInfo::AS_CXX11, "eosio::contract"},
          Spelling{AttributeCommonInfo::AS_GNU, "forge_contract"},
          Spelling{AttributeCommonInfo::AS_GNU, "eosio_contract"},
      };
      Spellings = spellings;
      OptArgs = 1;
   }

   bool diagAppertainsToDecl(clang::Sema& sema, const ParsedAttr& attribute,
                             const clang::Decl* declaration) const override {
      if (!require_scope(sema, attribute)) {
         return false;
      }
      if (llvm::isa<clang::CXXRecordDecl>(declaration) || llvm::isa<clang::CXXMethodDecl>(declaration)) {
         return true;
      }
      const auto diagnostic = sema.getDiagnostics().getCustomDiagID(
          clang::DiagnosticsEngine::Error, "contract attribute applies only to a class, struct, or member function");
      sema.Diag(attribute.getLoc(), diagnostic);
      return false;
   }

   AttrHandling handleDeclAttribute(clang::Sema& sema, clang::Decl* declaration,
                                    const ParsedAttr& attribute) const override {
      auto annotation = std::string{"forge.contract"};
      if (attribute.getNumArgs() != 0U) {
         annotation += ':';
         annotation += attribute_argument(sema, attribute);
      }
      declaration->addAttr(clang::AnnotateAttr::Create(sema.Context, annotation, nullptr, 0, attribute));
      preserve_attribute_scope(sema, declaration, attribute, "contract");
      return AttributeApplied;
   }
};

class action_attribute final : public ParsedAttrInfo {
 public:
   action_attribute() {
      static constexpr auto spellings = std::array{
          Spelling{AttributeCommonInfo::AS_CXX11, "action"},
          Spelling{AttributeCommonInfo::AS_CXX11, "forge::action"},
          Spelling{AttributeCommonInfo::AS_CXX11, "eosio::action"},
          Spelling{AttributeCommonInfo::AS_GNU, "forge_action"},
          Spelling{AttributeCommonInfo::AS_GNU, "eosio_action"},
      };
      Spellings = spellings;
      OptArgs = 1;
   }

   bool diagAppertainsToDecl(clang::Sema& sema, const ParsedAttr& attribute,
                             const clang::Decl* declaration) const override {
      if (!require_scope(sema, attribute)) {
         return false;
      }
      if (llvm::isa<clang::CXXRecordDecl>(declaration)) {
         return true;
      }
      const auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(declaration);
      if (method == nullptr || method->isStatic()) {
         const auto diagnostic = sema.getDiagnostics().getCustomDiagID(
             clang::DiagnosticsEngine::Error,
             "action attribute applies only to a class, struct, or non-static member function");
         sema.Diag(attribute.getLoc(), diagnostic);
         return false;
      }
      return true;
   }

   AttrHandling handleDeclAttribute(clang::Sema& sema, clang::Decl* declaration,
                                    const ParsedAttr& attribute) const override {
      auto annotation = std::string{"forge.action"};
      if (attribute.getNumArgs() != 0U) {
         annotation += ':';
         annotation += attribute_argument(sema, attribute);
      }
      declaration->addAttr(clang::AnnotateAttr::Create(sema.Context, annotation, nullptr, 0, attribute));
      preserve_attribute_scope(sema, declaration, attribute, "action");
      return AttributeApplied;
   }
};

class call_attribute final : public ParsedAttrInfo {
 public:
   call_attribute() {
      static constexpr auto spellings = std::array{
          Spelling{AttributeCommonInfo::AS_CXX11, "call"},
          Spelling{AttributeCommonInfo::AS_CXX11, "forge::call"},
          Spelling{AttributeCommonInfo::AS_CXX11, "eosio::call"},
          Spelling{AttributeCommonInfo::AS_GNU, "forge_call"},
          Spelling{AttributeCommonInfo::AS_GNU, "eosio_call"},
      };
      Spellings = spellings;
      OptArgs = 1;
   }

   bool diagAppertainsToDecl(clang::Sema& sema, const ParsedAttr& attribute,
                             const clang::Decl* declaration) const override {
      if (!require_scope(sema, attribute) ||
          !require_declaration<clang::CXXMethodDecl>(sema, attribute, const_cast<clang::Decl*>(declaration),
                                                     "call attribute applies only to a non-static member function")) {
         return false;
      }
      if (llvm::cast<clang::CXXMethodDecl>(declaration)->isStatic()) {
         const auto diagnostic = sema.getDiagnostics().getCustomDiagID(
             clang::DiagnosticsEngine::Error, "call attribute applies only to a non-static member function");
         sema.Diag(attribute.getLoc(), diagnostic);
         return false;
      }
      return true;
   }

   AttrHandling handleDeclAttribute(clang::Sema& sema, clang::Decl* declaration,
                                    const ParsedAttr& attribute) const override {
      auto annotation = std::string{"forge.call"};
      if (attribute.getNumArgs() != 0U) {
         annotation += ':';
         annotation += attribute_argument(sema, attribute);
      }
      declaration->addAttr(clang::AnnotateAttr::Create(sema.Context, annotation, nullptr, 0, attribute));
      preserve_attribute_scope(sema, declaration, attribute, "call");
      return AttributeApplied;
   }
};

class table_attribute final : public ParsedAttrInfo {
 public:
   table_attribute() {
      static constexpr auto spellings = std::array{
          Spelling{AttributeCommonInfo::AS_CXX11, "table"},
          Spelling{AttributeCommonInfo::AS_CXX11, "forge::table"},
          Spelling{AttributeCommonInfo::AS_CXX11, "eosio::table"},
          Spelling{AttributeCommonInfo::AS_GNU, "forge_table"},
          Spelling{AttributeCommonInfo::AS_GNU, "eosio_table"},
      };
      Spellings = spellings;
      OptArgs = 1;
   }

   bool diagAppertainsToDecl(clang::Sema& sema, const ParsedAttr& attribute,
                             const clang::Decl* declaration) const override {
      return require_scope(sema, attribute) &&
             require_declaration<clang::CXXRecordDecl>(sema, attribute, const_cast<clang::Decl*>(declaration),
                                                       "table attribute applies only to a class or struct");
   }

   AttrHandling handleDeclAttribute(clang::Sema& sema, clang::Decl* declaration,
                                    const ParsedAttr& attribute) const override {
      auto annotation = std::string{"forge.table"};
      if (attribute.getNumArgs() != 0U) {
         annotation += ':';
         annotation += attribute_argument(sema, attribute);
      }
      declaration->addAttr(clang::AnnotateAttr::Create(sema.Context, annotation, nullptr, 0, attribute));
      preserve_attribute_scope(sema, declaration, attribute, "table");
      return AttributeApplied;
   }
};

class ignore_attribute final : public canonical_attribute {
 public:
   ignore_attribute() : canonical_attribute{"ignore", declaration_kind::record, true} {
      static constexpr auto spellings = std::array{
          Spelling{AttributeCommonInfo::AS_CXX11, "ignore"},
          Spelling{AttributeCommonInfo::AS_CXX11, "forge::ignore"},
          Spelling{AttributeCommonInfo::AS_CXX11, "eosio::ignore"},
          Spelling{AttributeCommonInfo::AS_GNU, "forge_ignore"},
          Spelling{AttributeCommonInfo::AS_GNU, "eosio_ignore"},
      };
      Spellings = spellings;
      OptArgs = 1;
   }
};

class notify_attribute final : public canonical_attribute {
 public:
   notify_attribute() : canonical_attribute{"on_notify", declaration_kind::method, true} {
      static constexpr auto spellings = std::array{
          Spelling{AttributeCommonInfo::AS_CXX11, "on_notify"},
          Spelling{AttributeCommonInfo::AS_CXX11, "forge::on_notify"},
          Spelling{AttributeCommonInfo::AS_CXX11, "eosio::on_notify"},
          Spelling{AttributeCommonInfo::AS_GNU, "forge_on_notify"},
          Spelling{AttributeCommonInfo::AS_GNU, "eosio_on_notify"},
      };
      Spellings = spellings;
      NumArgs = 1;
   }
};

class ricardian_attribute final : public canonical_attribute {
 public:
   ricardian_attribute() : canonical_attribute{"ricardian", declaration_kind::record_or_method, true} {
      static constexpr auto spellings = std::array{
          Spelling{AttributeCommonInfo::AS_CXX11, "ricardian"},
          Spelling{AttributeCommonInfo::AS_CXX11, "forge::ricardian"},
          Spelling{AttributeCommonInfo::AS_CXX11, "eosio::ricardian"},
          Spelling{AttributeCommonInfo::AS_GNU, "forge_ricardian"},
          Spelling{AttributeCommonInfo::AS_GNU, "eosio_ricardian"},
      };
      Spellings = spellings;
      NumArgs = 1;
   }
};

class read_only_attribute final : public canonical_attribute {
 public:
   read_only_attribute() : canonical_attribute{"read_only", declaration_kind::record_or_method, false} {
      static constexpr auto spellings = std::array{
          Spelling{AttributeCommonInfo::AS_CXX11, "read_only"},
          Spelling{AttributeCommonInfo::AS_CXX11, "forge::read_only"},
          Spelling{AttributeCommonInfo::AS_CXX11, "eosio::read_only"},
          Spelling{AttributeCommonInfo::AS_GNU, "forge_read_only"},
          Spelling{AttributeCommonInfo::AS_GNU, "eosio_read_only"},
      };
      Spellings = spellings;
   }
};

template <const char* Kind, const char* Basename, bool Argument> class wasm_attribute final : public canonical_attribute {
 public:
   wasm_attribute() : canonical_attribute{Kind, declaration_kind::function, Argument} {
      static const auto forge_name = std::string{"forge::"} + Basename;
      static const auto eosio_name = std::string{"eosio::"} + Basename;
      static const auto forge_gnu = std::string{"forge_"} + Basename;
      static const auto eosio_gnu = std::string{"eosio_"} + Basename;
      static const auto spellings = std::array{
          Spelling{AttributeCommonInfo::AS_CXX11, Basename},
          Spelling{AttributeCommonInfo::AS_CXX11, forge_name.c_str()},
          Spelling{AttributeCommonInfo::AS_CXX11, eosio_name.c_str()},
          Spelling{AttributeCommonInfo::AS_GNU, forge_gnu.c_str()},
          Spelling{AttributeCommonInfo::AS_GNU, eosio_gnu.c_str()},
      };
      Spellings = spellings;
      if constexpr (Argument) {
         OptArgs = 1;
      }
   }
};

inline constexpr char wasm_action_kind[] = "wasm_action";
inline constexpr char wasm_action_name[] = "wasm_action";
inline constexpr char wasm_notify_kind[] = "wasm_notify";
inline constexpr char wasm_notify_name[] = "wasm_notify";
inline constexpr char wasm_abi_kind[] = "wasm_abi";
inline constexpr char wasm_abi_name[] = "wasm_abi";
inline constexpr char wasm_entry_kind[] = "wasm_entry";
inline constexpr char wasm_entry_name[] = "wasm_entry";
inline constexpr char wasm_import_kind[] = "wasm_import";
inline constexpr char wasm_import_name[] = "wasm_import";

using wasm_action_attribute = wasm_attribute<wasm_action_kind, wasm_action_name, true>;
using wasm_notify_attribute = wasm_attribute<wasm_notify_kind, wasm_notify_name, true>;
using wasm_abi_attribute = wasm_attribute<wasm_abi_kind, wasm_abi_name, true>;
using wasm_entry_attribute = wasm_attribute<wasm_entry_kind, wasm_entry_name, false>;
using wasm_import_attribute = wasm_attribute<wasm_import_kind, wasm_import_name, false>;

struct registrations {
   clang::ParsedAttrInfoRegistry::Add<contract_attribute> contract{"forge_contract", "Forge contract class"};
   clang::ParsedAttrInfoRegistry::Add<action_attribute> action{"forge_action", "Forge contract action"};
   clang::ParsedAttrInfoRegistry::Add<call_attribute> call{"forge_call", "Forge synchronous call"};
   clang::ParsedAttrInfoRegistry::Add<table_attribute> table{"forge_table", "Forge contract table"};
   clang::ParsedAttrInfoRegistry::Add<ignore_attribute> ignore{"forge_ignore", "Forge ignored ABI record"};
   clang::ParsedAttrInfoRegistry::Add<notify_attribute> notify{"forge_on_notify", "Forge notification handler"};
   clang::ParsedAttrInfoRegistry::Add<ricardian_attribute> ricardian{"forge_ricardian",
                                                                     "Forge Ricardian declaration"};
   clang::ParsedAttrInfoRegistry::Add<read_only_attribute> read_only{"forge_read_only", "Forge read-only entry"};
   clang::ParsedAttrInfoRegistry::Add<wasm_action_attribute> wasm_action{"forge_wasm_action",
                                                                         "Forge lowered action entry"};
   clang::ParsedAttrInfoRegistry::Add<wasm_notify_attribute> wasm_notify{"forge_wasm_notify",
                                                                         "Forge lowered notification entry"};
   clang::ParsedAttrInfoRegistry::Add<wasm_abi_attribute> wasm_abi{"forge_wasm_abi", "Forge WebAssembly ABI"};
   clang::ParsedAttrInfoRegistry::Add<wasm_entry_attribute> wasm_entry{"forge_wasm_entry",
                                                                        "Forge WebAssembly entry point"};
   clang::ParsedAttrInfoRegistry::Add<wasm_import_attribute> wasm_import{"forge_wasm_import",
                                                                         "Forge WebAssembly import"};
};

} // namespace

namespace forge::contract::attributes {

void register_all() {
   static auto values = registrations{};
   static_cast<void>(values);
}

} // namespace forge::contract::attributes
