#include <clang/AST/Attr.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/Expr.h>
#include <clang/Basic/ParsedAttrInfo.h>
#include <clang/Sema/ParsedAttr.h>
#include <clang/Sema/Sema.h>

#include <array>
#include <string>

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
   if (attribute.getScopeName() != nullptr) {
      return true;
   }
   const auto diagnostic = sema.getDiagnostics().getCustomDiagID(
       clang::DiagnosticsEngine::Error, "Forge contract attributes require the 'forge' or 'eosio' namespace");
   sema.Diag(attribute.getLoc(), diagnostic);
   return false;
}

class contract_attribute final : public ParsedAttrInfo {
 public:
   contract_attribute() {
      static constexpr auto spellings = std::array{
          // Clang probes the unqualified basename before parsing arguments of a scoped plugin attribute.
          Spelling{AttributeCommonInfo::AS_CXX11, "contract"},
          Spelling{AttributeCommonInfo::AS_CXX11, "forge::contract"},
          Spelling{AttributeCommonInfo::AS_CXX11, "eosio::contract"},
      };
      Spellings = spellings;
      OptArgs = 1;
   }

   bool diagAppertainsToDecl(clang::Sema& sema, const ParsedAttr& attribute,
                             const clang::Decl* declaration) const override {
      return require_scope(sema, attribute) &&
             require_declaration<clang::CXXRecordDecl>(sema, attribute, const_cast<clang::Decl*>(declaration),
                                                       "contract attribute applies only to a class or struct");
   }

   AttrHandling handleDeclAttribute(clang::Sema& sema, clang::Decl* declaration,
                                    const ParsedAttr& attribute) const override {
      auto annotation = std::string{"forge.contract"};
      if (attribute.getNumArgs() != 0U) {
         annotation += ':';
         annotation += attribute_argument(sema, attribute);
      }
      declaration->addAttr(clang::AnnotateAttr::Create(sema.Context, annotation, nullptr, 0, attribute));
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
      };
      Spellings = spellings;
      OptArgs = 1;
   }

   bool diagAppertainsToDecl(clang::Sema& sema, const ParsedAttr& attribute,
                             const clang::Decl* declaration) const override {
      if (!require_scope(sema, attribute) ||
          !require_declaration<clang::CXXMethodDecl>(sema, attribute, const_cast<clang::Decl*>(declaration),
                                                     "action attribute applies only to a non-static member function")) {
         return false;
      }
      if (llvm::cast<clang::CXXMethodDecl>(declaration)->isStatic()) {
         const auto diagnostic = sema.getDiagnostics().getCustomDiagID(
             clang::DiagnosticsEngine::Error, "action attribute applies only to a non-static member function");
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
      return AttributeApplied;
   }
};

clang::ParsedAttrInfoRegistry::Add<contract_attribute> register_contract{"forge_contract", "Forge contract class"};
clang::ParsedAttrInfoRegistry::Add<action_attribute> register_action{"forge_action", "Forge contract action"};
clang::ParsedAttrInfoRegistry::Add<call_attribute> register_call{"forge_call", "Forge synchronous call"};
clang::ParsedAttrInfoRegistry::Add<table_attribute> register_table{"forge_table", "Forge contract table"};

} // namespace
