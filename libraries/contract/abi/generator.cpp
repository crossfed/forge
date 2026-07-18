module;

#include <clang/AST/Attr.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/TypeLoc.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Index/USRGeneration.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/Support/DynamicLibrary.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module forge.contract.abi.generator;

import forge.chain.protocol.abi;
import forge.chain.protocol.types;
import forge.codec.json;
import forge.variant.value;

namespace {

namespace protocol = forge::chain::protocol;

struct field_shape {
   std::string name;
   std::string type;
};

struct struct_shape {
   std::string name;
   std::string base;
   std::vector<field_shape> fields;
};

struct record_codec_shape {
   std::string name;
   std::string base;
   std::string forward_declaration;
   std::vector<std::string> fields;
};

struct action_shape {
   std::string name;
   std::string type;
   std::string result;
   std::string class_name;
   std::string method_name;
};

struct type_shape {
   std::string name;
   std::string type;
};

struct variant_shape {
   std::string name;
   std::vector<std::string> types;
};

struct table_shape {
   std::string name;
   std::string type;
};

struct call_shape {
   std::string name;
   std::string type;
   std::string result;
   std::uint64_t id = 0;
};

struct schema {
   std::vector<type_shape> types;
   std::vector<struct_shape> structs;
   std::vector<record_codec_shape> record_codecs;
   std::vector<action_shape> actions;
   std::vector<variant_shape> variants;
   std::vector<table_shape> tables;
   std::vector<protocol::clause_pair> clauses;
   std::vector<call_shape> calls;
   std::map<std::string, std::string> type_declarations;
   std::map<std::string, std::string> struct_declarations;
   std::set<std::string> variant_names;
   std::map<std::string, std::string> table_declarations;
   std::map<std::string, std::string> inferred_table_names;
   std::set<std::string> explicit_table_declarations;
   std::map<std::string, std::string> action_declarations;
   std::map<std::string, std::string> action_methods;
   std::map<std::string, std::string> call_declarations;
   std::map<std::filesystem::path, std::set<std::string>> source_record_codecs;
   bool has_apply = false;
   bool has_eosio_dispatch = false;
   bool failed = false;
};

std::string make_forward_declaration(const clang::RecordDecl& declaration) {
   if (declaration.getIdentifier() == nullptr || llvm::isa<clang::ClassTemplateSpecializationDecl>(declaration)) {
      return {};
   }

   auto namespaces = std::vector<const clang::NamespaceDecl*>{};
   auto* context = declaration.getDeclContext();
   while (const auto* current = llvm::dyn_cast<clang::NamespaceDecl>(context)) {
      if (current->isAnonymousNamespace()) {
         return {};
      }
      namespaces.push_back(current);
      context = current->getDeclContext();
   }
   if (!context->isTranslationUnit()) {
      return {};
   }

   auto output = std::ostringstream{};
   for (auto iterator = namespaces.rbegin(); iterator != namespaces.rend(); ++iterator) {
      if ((*iterator)->isInline()) {
         output << "inline ";
      }
      output << "namespace " << (*iterator)->getNameAsString() << " { ";
   }
   output << (declaration.isClass() ? "class " : "struct ") << declaration.getNameAsString() << ';';
   for (auto index = std::size_t{0}; index < namespaces.size(); ++index) {
      output << " }";
   }
   return output.str();
}

bool has_nameable_scope(const clang::RecordDecl& declaration) {
   if (declaration.getIdentifier() == nullptr) {
      return false;
   }

   auto* context = declaration.getDeclContext();
   while (context != nullptr) {
      if (context->isTranslationUnit()) {
         return true;
      }
      if (const auto* current = llvm::dyn_cast<clang::NamespaceDecl>(context)) {
         if (current->isAnonymousNamespace()) {
            return false;
         }
         context = current->getParent();
         continue;
      }
      if (const auto* current = llvm::dyn_cast<clang::RecordDecl>(context)) {
         if (current->getIdentifier() == nullptr) {
            return false;
         }
         context = current->getDeclContext();
         continue;
      }
      if (const auto* current = llvm::dyn_cast<clang::LinkageSpecDecl>(context)) {
         context = current->getParent();
         continue;
      }
      return false;
   }
   return false;
}

std::string declaration_identity(const clang::NamedDecl& declaration) {
   auto identity = llvm::SmallString<128>{};
   if (!clang::index::generateUSRForDecl(&declaration, identity) && !identity.empty()) {
      return identity.str().str();
   }
   auto fallback = declaration.getQualifiedNameAsString();
   if (const auto* value = llvm::dyn_cast<clang::ValueDecl>(&declaration); value != nullptr) {
      fallback += ':' + value->getType().getCanonicalType().getAsString();
   }
   return fallback;
}

bool claim_struct(schema& output, clang::ASTContext& context, const std::string& name, std::string identity,
                  clang::SourceLocation location = {}) {
   const auto [existing, inserted] = output.struct_declarations.try_emplace(name, std::move(identity));
   if (inserted) {
      return true;
   }
   if (existing->second == identity) {
      return false;
   }
   const auto id = context.getDiagnostics().getCustomDiagID(clang::DiagnosticsEngine::Error,
                                                            "duplicate contract ABI struct name '%0'");
   context.getDiagnostics().Report(location, id) << name;
   output.failed = true;
   return false;
}

std::optional<std::string> annotation(const clang::Decl& declaration, std::string_view prefix) {
   const auto expected = llvm::StringRef{prefix.data(), prefix.size()};
   for (const auto* attribute : declaration.specific_attrs<clang::AnnotateAttr>()) {
      const auto value = attribute->getAnnotation();
      if (value == expected) {
         return std::string{};
      }
      if (value.starts_with(prefix) && value.size() > prefix.size() && value[prefix.size()] == ':') {
         return value.substr(prefix.size() + 1U).str();
      }
   }
   return std::nullopt;
}

bool has_annotation(const clang::Decl& declaration, std::string_view expected) {
   const auto value = llvm::StringRef{expected.data(), expected.size()};
   return std::ranges::any_of(declaration.specific_attrs<clang::AnnotateAttr>(),
                              [&](const auto* attribute) { return attribute->getAnnotation() == value; });
}

std::string record_name(const clang::RecordDecl& declaration) {
   if (declaration.getIdentifier() == nullptr) {
      throw std::runtime_error{"anonymous records are not supported in contract ABI"};
   }
   return declaration.getNameAsString();
}

bool is_global_function(const clang::FunctionDecl& declaration) {
   auto* context = declaration.getDeclContext();
   while (llvm::isa_and_nonnull<clang::LinkageSpecDecl>(context)) {
      context = context->getParent();
   }
   return context != nullptr && context->isTranslationUnit();
}

class type_encoder {
 public:
   type_encoder(clang::ASTContext& context, schema& output, std::set<std::string>& source_record_codecs)
       : context_(context), output_(output), source_record_codecs_(source_record_codecs) {}

   std::string encode(clang::QualType input) {
      auto type = input.getNonReferenceType().getUnqualifiedType();
      if (const auto* alias = llvm::dyn_cast_or_null<clang::TypedefType>(type.getTypePtrOrNull())) {
         const auto* declaration = alias->getDecl();
         if (!context_.getSourceManager().isInSystemHeader(declaration->getLocation())) {
            const auto name = declaration->getNameAsString();
            const auto target = encode(declaration->getUnderlyingType());
            add_alias(name, target, declaration->getLocation());
            return name.empty() ? target : name;
         }
         type = alias->desugar().getUnqualifiedType();
      }
      type = type.getDesugaredType(context_).getUnqualifiedType();
      if (const auto* builtin = type->getAs<clang::BuiltinType>()) {
         return encode_builtin(*builtin);
      }
      if (const auto* enumeration = type->getAs<clang::EnumType>()) {
         return encode(enumeration->getDecl()->getIntegerType());
      }
      if (const auto* array = context_.getAsConstantArrayType(type)) {
         return encode(array->getElementType()) + '[' + llvm::toString(array->getSize(), 10, false) + ']';
      }
      if (const auto* record = type->getAs<clang::RecordType>()) {
         return encode_record(*record->getDecl());
      }
      fail(type.getAsString(), {});
      return {};
   }

   std::string encode(const clang::DeclaratorDecl& declaration) {
      const auto* source = declaration.getTypeSourceInfo();
      return source == nullptr ? encode(declaration.getType()) : encode_location(source->getTypeLoc());
   }

   void add_table(const clang::CXXRecordDecl& declaration, std::string name, bool inferred = false) {
      const auto identity = declaration_identity(declaration);
      if (inferred && output_.explicit_table_declarations.contains(identity)) {
         return;
      }
      if (!inferred) {
         output_.explicit_table_declarations.insert(identity);
         if (const auto existing = output_.inferred_table_names.find(identity);
             existing != output_.inferred_table_names.end()) {
            const auto inferred_name = existing->second;
            output_.tables.erase(std::remove_if(output_.tables.begin(), output_.tables.end(),
                                                [&](const table_shape& table) { return table.name == inferred_name; }),
                                 output_.tables.end());
            output_.table_declarations.erase(inferred_name);
            output_.inferred_table_names.erase(existing);
         }
      }
      if (name.empty()) {
         name = record_name(declaration);
      }
      const auto [existing, inserted] = output_.table_declarations.try_emplace(name, identity);
      if (!inserted && existing->second == identity) {
         return;
      }
      if (!inserted) {
         const auto id = context_.getDiagnostics().getCustomDiagID(clang::DiagnosticsEngine::Error,
                                                                   "duplicate contract ABI table name '%0'");
         context_.getDiagnostics().Report(declaration.getLocation(), id) << name;
         output_.failed = true;
         return;
      }
      output_.tables.push_back(table_shape{std::move(name), encode_record(declaration)});
      if (inferred) {
         output_.inferred_table_names.try_emplace(identity, output_.tables.back().name);
      }
   }

 private:
   static bool is_std_template(const clang::TemplateDecl& declaration, std::string_view name) {
      if (declaration.getName() != llvm::StringRef{name.data(), name.size()}) {
         return false;
      }
      auto* context = declaration.getDeclContext();
      while (const auto* current = llvm::dyn_cast<clang::NamespaceDecl>(context)) {
         if (current->isStdNamespace()) {
            return true;
         }
         if (!current->isInline()) {
            return false;
         }
         context = current->getParent();
      }
      return false;
   }

   static clang::TypeLoc unwrap(clang::TypeLoc location) {
      while (true) {
         if (const auto qualified = location.getAs<clang::QualifiedTypeLoc>(); !qualified.isNull()) {
            location = qualified.getUnqualifiedLoc();
         } else if (const auto reference = location.getAs<clang::ReferenceTypeLoc>(); !reference.isNull()) {
            location = reference.getPointeeLoc();
         } else {
            return location;
         }
      }
   }

   std::string encode_location(clang::TypeLoc input) {
      const auto location = unwrap(input);
      const auto specialization = location.getAs<clang::TemplateSpecializationTypeLoc>();
      if (specialization.isNull()) {
         return encode(location.getType());
      }
      const auto* declaration = specialization.getTypePtr()->getTemplateName().getAsTemplateDecl();
      if (declaration == nullptr) {
         return encode(location.getType());
      }
      const auto type_argument = [&](std::size_t index) -> clang::TypeLoc {
         const auto& argument = specialization.getArgLoc(static_cast<unsigned>(index));
         const auto* source = argument.getTypeSourceInfo();
         return source == nullptr ? clang::TypeLoc{} : source->getTypeLoc();
      };
      if ((is_std_template(*declaration, "vector") || is_std_template(*declaration, "set") ||
           is_std_template(*declaration, "deque") || is_std_template(*declaration, "list")) &&
          specialization.getNumArgs() >= 1U) {
         return encode_location(type_argument(0)) + "[]";
      }
      if (is_std_template(*declaration, "optional") && specialization.getNumArgs() >= 1U) {
         return encode_location(type_argument(0)) + '?';
      }
      if (is_std_template(*declaration, "array") && specialization.getNumArgs() >= 2U) {
         return encode_location(type_argument(0)) + '[' +
                std::to_string(integral_argument(specialization.getArgLoc(1))) + ']';
      }
      if ((is_std_template(*declaration, "pair") || is_std_template(*declaration, "map")) &&
          specialization.getNumArgs() >= 2U) {
         const auto first = type_argument(0);
         const auto second = type_argument(1);
         const auto pair = add_pair(template_part_location(first), template_part_location(second));
         return is_std_template(*declaration, "map") ? pair + "[]" : pair;
      }
      if (is_std_template(*declaration, "tuple")) {
         auto result = std::string{"tuple"};
         auto fields = std::vector<field_shape>{};
         for (auto index = std::size_t{0}; index < specialization.getNumArgs(); ++index) {
            const auto argument = type_argument(index);
            const auto part = template_part_location(argument);
            result += '_' + part;
            fields.push_back(field_shape{"field_" + std::to_string(index), part});
         }
         add_synthetic_struct(result, std::move(fields));
         return result;
      }
      if (is_std_template(*declaration, "variant")) {
         auto shape = variant_shape{.name = "variant"};
         for (auto index = std::size_t{0}; index < specialization.getNumArgs(); ++index) {
            const auto argument = type_argument(index);
            shape.name += '_' + template_part_location(argument);
            shape.types.push_back(encode_location(argument));
         }
         if (output_.variant_names.insert(shape.name).second) {
            output_.variants.push_back(shape);
         }
         return shape.name;
      }
      return encode(location.getType());
   }

   std::string template_part_location(clang::TypeLoc input) {
      const auto location = unwrap(input);
      const auto encoded = encode_location(location);
      const auto specialization = location.getAs<clang::TemplateSpecializationTypeLoc>();
      if (specialization.isNull()) {
         return encoded;
      }
      const auto* declaration = specialization.getTypePtr()->getTemplateName().getAsTemplateDecl();
      if (declaration == nullptr || is_std_template(*declaration, "basic_string")) {
         return encoded;
      }
      auto result = std::string{"B_"} + declaration->getNameAsString();
      for (auto index = std::size_t{0}; index < specialization.getNumArgs(); ++index) {
         const auto& argument = specialization.getArgLoc(static_cast<unsigned>(index));
         if (const auto* source = argument.getTypeSourceInfo(); source != nullptr) {
            result += '_' + template_part_location(source->getTypeLoc());
         } else if (argument.getArgument().getKind() == clang::TemplateArgument::Integral ||
                    argument.getArgument().getKind() == clang::TemplateArgument::Expression) {
            result += '_' + std::to_string(integral_argument(argument));
         }
      }
      result += "_E";
      add_alias(result, encoded);
      return result;
   }

   std::uint64_t integral_argument(const clang::TemplateArgumentLoc& location) const {
      const auto& argument = location.getArgument();
      if (argument.getKind() == clang::TemplateArgument::Integral) {
         return argument.getAsIntegral().getZExtValue();
      }
      if (argument.getKind() == clang::TemplateArgument::Expression) {
         auto value = clang::Expr::EvalResult{};
         if (argument.getAsExpr()->EvaluateAsInt(value, context_)) {
            return value.Val.getInt().getZExtValue();
         }
      }
      fail("non-integral template argument", location.getLocation());
      return 0;
   }

   std::string encode_builtin(const clang::BuiltinType& type) const {
      switch (type.getKind()) {
      case clang::BuiltinType::Bool:
         return "bool";
      case clang::BuiltinType::Char_S:
      case clang::BuiltinType::SChar:
         return "int8";
      case clang::BuiltinType::Char_U:
      case clang::BuiltinType::UChar:
         return "uint8";
      case clang::BuiltinType::Short:
         return "int16";
      case clang::BuiltinType::UShort:
         return "uint16";
      case clang::BuiltinType::Int:
         return "int32";
      case clang::BuiltinType::UInt:
         return "uint32";
      case clang::BuiltinType::Long:
         return context_.getTypeSize(clang::QualType{&type, 0}) == 32U ? "int32" : "int64";
      case clang::BuiltinType::LongLong:
         return "int64";
      case clang::BuiltinType::ULong:
         return context_.getTypeSize(clang::QualType{&type, 0}) == 32U ? "uint32" : "uint64";
      case clang::BuiltinType::ULongLong:
         return "uint64";
      case clang::BuiltinType::Int128:
         return "int128";
      case clang::BuiltinType::UInt128:
         return "uint128";
      case clang::BuiltinType::Float:
         return "float32";
      case clang::BuiltinType::Double:
         return "float64";
      case clang::BuiltinType::LongDouble:
         return "float128";
      default:
         break;
      }
      fail(type.getName(clang::PrintingPolicy{context_.getLangOpts()}).str(), {});
      return {};
   }

   std::string encode_record(const clang::RecordDecl& declaration) {
      const auto* record = &declaration;
      if (const auto* cpp = llvm::dyn_cast<clang::CXXRecordDecl>(record); cpp != nullptr) {
         if (const auto* definition = cpp->getDefinition(); definition != nullptr) {
            record = definition;
         }
      }
      const auto qualified = record->getQualifiedNameAsString();
      const auto* specialization = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(record);
      if (specialization != nullptr && is_std_template(*specialization->getSpecializedTemplate(), "basic_string")) {
         return "string";
      }

      static const auto known = std::vector<std::pair<std::string_view, std::string_view>>{
          {"forge::chain::protocol::name", "name"},
          {"forge::chain::protocol::symbol_code", "symbol_code"},
          {"forge::chain::protocol::symbol", "symbol"},
          {"forge::chain::protocol::asset", "asset"},
          {"forge::chain::protocol::permission_level", "permission_level"},
      };
      for (const auto& [cpp_name, abi_name] : known) {
         if (qualified == cpp_name) {
            return std::string{abi_name};
         }
      }

      if (specialization != nullptr) {
         const auto template_qualified = specialization->getSpecializedTemplate()->getQualifiedNameAsString();
         const auto arguments = flatten(specialization->getTemplateArgs());
         if (template_qualified == "forge::chain::protocol::fixed_key" && arguments.size() >= 1U &&
             arguments[0].getKind() == clang::TemplateArgument::Integral &&
             arguments[0].getAsIntegral().getZExtValue() == 32U) {
            return "checksum256";
         }
         if ((is_std_template(*specialization->getSpecializedTemplate(), "vector") ||
              is_std_template(*specialization->getSpecializedTemplate(), "set") ||
              is_std_template(*specialization->getSpecializedTemplate(), "deque") ||
              is_std_template(*specialization->getSpecializedTemplate(), "list")) &&
             arguments.size() >= 1U && arguments[0].getKind() == clang::TemplateArgument::Type) {
            return encode(arguments[0].getAsType()) + "[]";
         }
         if (is_std_template(*specialization->getSpecializedTemplate(), "optional") && arguments.size() >= 1U &&
             arguments[0].getKind() == clang::TemplateArgument::Type) {
            return encode(arguments[0].getAsType()) + '?';
         }
         if (is_std_template(*specialization->getSpecializedTemplate(), "array") && arguments.size() >= 2U &&
             arguments[0].getKind() == clang::TemplateArgument::Type &&
             arguments[1].getKind() == clang::TemplateArgument::Integral) {
            return encode(arguments[0].getAsType()) + '[' +
                   std::to_string(arguments[1].getAsIntegral().getZExtValue()) + ']';
         }
         if (is_std_template(*specialization->getSpecializedTemplate(), "pair") && arguments.size() >= 2U) {
            return add_pair(arguments[0].getAsType(), arguments[1].getAsType());
         }
         if (is_std_template(*specialization->getSpecializedTemplate(), "map") && arguments.size() >= 2U) {
            return add_pair(arguments[0].getAsType(), arguments[1].getAsType()) + "[]";
         }
         if (is_std_template(*specialization->getSpecializedTemplate(), "tuple")) {
            return add_tuple(arguments);
         }
         if (is_std_template(*specialization->getSpecializedTemplate(), "variant")) {
            return add_variant(arguments);
         }
      }

      const auto name = abi_record_name(*record);
      add_struct(*record, name);
      return name;
   }

   static std::vector<clang::TemplateArgument> flatten(const clang::TemplateArgumentList& input) {
      auto result = std::vector<clang::TemplateArgument>{};
      for (const auto& argument : input.asArray()) {
         if (argument.getKind() == clang::TemplateArgument::Pack) {
            result.insert(result.end(), argument.pack_begin(), argument.pack_end());
         } else {
            result.push_back(argument);
         }
      }
      return result;
   }

   std::string abi_record_name(const clang::RecordDecl& declaration) {
      const auto* specialization = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(&declaration);
      if (specialization == nullptr) {
         return record_name(declaration);
      }

      auto result = specialization->getSpecializedTemplate()->getNameAsString();
      for (const auto& argument : flatten(specialization->getTemplateArgs())) {
         if (argument.getKind() == clang::TemplateArgument::Integral) {
            result += '_' + std::to_string(argument.getAsIntegral().getZExtValue());
         } else if (argument.getKind() == clang::TemplateArgument::Type) {
            result += '_' + template_part(argument.getAsType());
         }
      }
      return result;
   }

   std::string template_part(clang::QualType input) {
      auto type = input.getNonReferenceType().getUnqualifiedType();
      if (const auto* alias = llvm::dyn_cast_or_null<clang::TypedefType>(type.getTypePtrOrNull())) {
         if (!context_.getSourceManager().isInSystemHeader(alias->getDecl()->getLocation())) {
            return encode(type);
         }
      }

      const auto encoded = encode(type);
      const auto* record = type.getDesugaredType(context_)->getAs<clang::RecordType>();
      const auto* specialization =
          record == nullptr ? nullptr : llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(record->getDecl());
      if (specialization == nullptr) {
         return encoded;
      }
      const auto* template_declaration = specialization->getSpecializedTemplate();
      const auto name = template_declaration->getNameAsString();
      if (is_std_template(*template_declaration, "basic_string")) {
         return encoded;
      }

      auto result = std::string{"B_"} + name;
      const auto arguments = flatten(specialization->getTemplateArgs());
      const auto semantic_arguments =
          is_std_template(*template_declaration, "map") || is_std_template(*template_declaration, "pair")
              ? std::min<std::size_t>(2U, arguments.size())
          : is_std_template(*template_declaration, "array") ? std::min<std::size_t>(2U, arguments.size())
                                                            : arguments.size();
      for (auto index = std::size_t{0}; index < semantic_arguments; ++index) {
         const auto& argument = arguments[index];
         if (argument.getKind() == clang::TemplateArgument::Type) {
            result += '_' + template_part(argument.getAsType());
         } else if (argument.getKind() == clang::TemplateArgument::Integral) {
            result += '_' + std::to_string(argument.getAsIntegral().getZExtValue());
         }
      }
      result += "_E";
      add_alias(result, encoded);
      return result;
   }

   void add_alias(const std::string& name, const std::string& target, clang::SourceLocation location = {}) {
      if (name.empty() || name == target) {
         return;
      }
      const auto [existing, inserted] = output_.type_declarations.try_emplace(name, target);
      if (inserted) {
         output_.types.push_back(type_shape{name, target});
         return;
      }
      if (existing->second != target) {
         const auto id = context_.getDiagnostics().getCustomDiagID(
             clang::DiagnosticsEngine::Error, "conflicting contract ABI type alias '%0' maps to both '%1' and '%2'");
         context_.getDiagnostics().Report(location, id) << name << existing->second << target;
         output_.failed = true;
      }
   }

   std::string add_pair(clang::QualType first, clang::QualType second) {
      return add_pair(template_part(first), template_part(second));
   }

   std::string add_pair(std::string first_name, std::string second_name) {
      const auto name = "pair_" + first_name + '_' + second_name;
      add_synthetic_struct(name, {{"first", std::move(first_name)}, {"second", std::move(second_name)}});
      return name;
   }

   std::string add_tuple(const std::vector<clang::TemplateArgument>& arguments) {
      auto name = std::string{"tuple"};
      auto fields = std::vector<field_shape>{};
      for (auto index = std::size_t{0}; index < arguments.size(); ++index) {
         if (arguments[index].getKind() != clang::TemplateArgument::Type) {
            continue;
         }
         const auto part = template_part(arguments[index].getAsType());
         name += '_' + part;
         fields.push_back(field_shape{"field_" + std::to_string(index), part});
      }
      add_synthetic_struct(name, std::move(fields));
      return name;
   }

   std::string add_variant(const std::vector<clang::TemplateArgument>& arguments) {
      auto shape = variant_shape{.name = "variant"};
      for (const auto& argument : arguments) {
         if (argument.getKind() != clang::TemplateArgument::Type) {
            continue;
         }
         shape.name += '_' + template_part(argument.getAsType());
         shape.types.push_back(encode(argument.getAsType()));
      }
      if (output_.variant_names.insert(shape.name).second) {
         output_.variants.push_back(shape);
      }
      return shape.name;
   }

   void add_synthetic_struct(std::string name, std::vector<field_shape> fields) {
      if (!claim_struct(output_, context_, name, "synthetic:" + name)) {
         return;
      }
      output_.structs.push_back(struct_shape{.name = std::move(name), .fields = std::move(fields)});
   }

   void add_struct(const clang::RecordDecl& declaration, const std::string& name) {
      const auto* record = &declaration;
      if (const auto* cpp = llvm::dyn_cast<clang::CXXRecordDecl>(record); cpp != nullptr) {
         if (const auto* definition = cpp->getDefinition(); definition != nullptr) {
            record = definition;
         }
      }
      if (!has_nameable_scope(*record)) {
         auto& diagnostics = context_.getDiagnostics();
         const auto id = diagnostics.getCustomDiagID(
             clang::DiagnosticsEngine::Error, "contract ABI record '%0' is declared in an anonymous or local scope");
         diagnostics.Report(declaration.getLocation(), id) << record_name(*record);
         output_.failed = true;
         return;
      }
      const auto codec_name = "::" + record->getQualifiedNameAsString();
      source_record_codecs_.insert(codec_name);
      if (!claim_struct(output_, context_, name, declaration_identity(declaration), declaration.getLocation())) {
         return;
      }

      auto shape = struct_shape{.name = name};
      auto codec = record_codec_shape{};
      if (record->isUnion()) {
         fail("union record", declaration.getLocation());
         return;
      }
      codec.name = codec_name;
      codec.forward_declaration = make_forward_declaration(*record);
      if (const auto* cpp = llvm::dyn_cast<clang::CXXRecordDecl>(record); cpp != nullptr && cpp->hasDefinition()) {
         if (cpp->getNumBases() > 1U) {
            fail("multiple ABI base classes", declaration.getLocation());
         } else if (cpp->getNumBases() == 1U) {
            const auto& base = *cpp->bases_begin();
            const auto implicit_public = base.getAccessSpecifier() == clang::AS_none && cpp->isStruct();
            if (base.isVirtual() || (base.getAccessSpecifier() != clang::AS_public && !implicit_public)) {
               fail("non-public or virtual ABI base class", base.getBeginLoc());
               return;
            }
            const auto* base_record = base.getType()->getAsCXXRecordDecl();
            if (base_record == nullptr) {
               fail("ABI base class", base.getBeginLoc());
               return;
            }
            shape.base = encode(base.getType());
            codec.base = "::" + base_record->getQualifiedNameAsString();
         }
      }
      for (const auto* field : record->fields()) {
         if (field->getIdentifier() == nullptr || field->isBitField() || field->getType().isConstQualified() ||
             field->getType()->isReferenceType() ||
             (field->getAccess() != clang::AS_public && field->getAccess() != clang::AS_none)) {
            fail("non-public, unnamed, const, reference, or bit-field ABI member", field->getLocation());
            return;
         }
         shape.fields.push_back(field_shape{field->getNameAsString(), encode(*field)});
         codec.fields.push_back(field->getNameAsString());
      }
      output_.structs.push_back(std::move(shape));
      output_.record_codecs.push_back(std::move(codec));
   }

   void fail(std::string_view type, clang::SourceLocation location) const {
      auto& diagnostics = context_.getDiagnostics();
      const auto id =
          diagnostics.getCustomDiagID(clang::DiagnosticsEngine::Error, "unsupported contract ABI type '%0'");
      diagnostics.Report(location, id) << type;
      output_.failed = true;
   }

   clang::ASTContext& context_;
   schema& output_;
   std::set<std::string>& source_record_codecs_;
};

class visitor final : public clang::RecursiveASTVisitor<visitor> {
 public:
   visitor(clang::ASTContext& context, schema& output, std::set<std::string>& source_record_codecs,
           std::string_view contract_name)
       : context_(context), output_(output), encoder_(context, output, source_record_codecs),
         contract_name_(contract_name) {}

   bool VisitCXXRecordDecl(clang::CXXRecordDecl* declaration) {
      if (!declaration->isThisDeclarationADefinition() || declaration->isImplicit()) {
         return true;
      }
      if (const auto table = annotation(*declaration, "forge.table"); table.has_value()) {
         const auto owner = owning_contract(*declaration);
         const auto selected = owner.has_value() && *owner == contract_name_;
         if (selected || (!owner.has_value() && !table->empty())) {
            encoder_.add_table(*declaration, *table, table->empty());
         }
         return true;
      }
      const auto contract = annotation(*declaration, "forge.contract");
      if (!contract.has_value()) {
         return true;
      }
      const auto declared_name = contract->empty() ? declaration->getNameAsString() : *contract;
      if (declared_name != contract_name_) {
         return true;
      }

      found_contract_ = true;
      for (const auto* method : declaration->methods()) {
         const auto action = annotation(*method, "forge.action");
         if (action.has_value()) {
            add_action(*declaration, *method, *action);
         }
         const auto call = annotation(*method, "forge.call");
         if (call.has_value()) {
            add_call(*method, *call);
         }
      }
      return true;
   }

   bool VisitFunctionDecl(clang::FunctionDecl* declaration) {
      if (declaration->isThisDeclarationADefinition() && has_annotation(*declaration, "forge.eosio_dispatch")) {
         output_.has_eosio_dispatch = true;
         return true;
      }
      if (!declaration->isThisDeclarationADefinition() || !is_global_function(*declaration) ||
          declaration->getIdentifier() == nullptr || declaration->getIdentifier()->getName() != "apply") {
         return true;
      }
      if (!declaration->isExternC() || !declaration->hasExternalFormalLinkage()) {
         return true;
      }
      if (!declaration->getReturnType()->isVoidType() || declaration->getNumParams() != 3U) {
         report(declaration->getLocation(), "contract apply entry point must be void(uint64_t, uint64_t, uint64_t)");
         return true;
      }
      for (const auto* parameter : declaration->parameters()) {
         if (!parameter->getType()->isIntegerType() || context_.getTypeSize(parameter->getType()) != 64U) {
            report(declaration->getLocation(), "contract apply entry point must be void(uint64_t, uint64_t, uint64_t)");
            return true;
         }
      }
      output_.has_apply = true;
      output_.has_eosio_dispatch = output_.has_eosio_dispatch || has_annotation(*declaration, "forge.eosio_dispatch");
      return true;
   }

   bool VisitClassTemplateSpecializationDecl(clang::ClassTemplateSpecializationDecl* declaration) {
      const auto* record = specialization_record(*declaration);
      if (record == nullptr || !belongs_to_selected_contract(*record)) {
         return true;
      }
      add_table(*declaration);
      return true;
   }

   bool VisitTypedefNameDecl(clang::TypedefNameDecl* declaration) {
      const auto* specialization = specialization_type(declaration->getUnderlyingType());
      const auto* record = specialization == nullptr ? nullptr : specialization_record(*specialization);
      const auto owned_record = record != nullptr && belongs_to_selected_contract(*record);
      if (specialization != nullptr && (belongs_to_selected_contract(*declaration) || owned_record)) {
         add_table(*specialization);
      }
      return true;
   }

   bool found_contract() const {
      return found_contract_;
   }

 private:
   const clang::ClassTemplateSpecializationDecl* specialization_type(clang::QualType type) const {
      const auto* record = type.getDesugaredType(context_).getTypePtrOrNull();
      const auto* record_type = record == nullptr ? nullptr : record->getAs<clang::RecordType>();
      return record_type == nullptr ? nullptr
                                    : llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(record_type->getDecl());
   }

   static const clang::CXXRecordDecl* specialization_record(const clang::ClassTemplateSpecializationDecl& declaration) {
      const auto template_name = declaration.getSpecializedTemplate()->getNameAsString();
      if (template_name != "multi_index" && template_name != "singleton") {
         return nullptr;
      }
      const auto& arguments = declaration.getTemplateArgs();
      if (arguments.size() < 2U || arguments[0].getKind() != clang::TemplateArgument::Integral ||
          arguments[1].getKind() != clang::TemplateArgument::Type) {
         return nullptr;
      }
      const auto* record = arguments[1].getAsType()->getAsCXXRecordDecl();
      return record == nullptr ? nullptr : record->getDefinition();
   }

   std::optional<std::string> owning_contract(const clang::Decl& declaration) const {
      if (const auto* record = llvm::dyn_cast<clang::CXXRecordDecl>(&declaration); record != nullptr) {
         const auto contract = annotation(*record, "forge.contract");
         if (contract.has_value()) {
            return contract->empty() ? record->getNameAsString() : *contract;
         }
      }
      for (auto* context = declaration.getDeclContext(); context != nullptr; context = context->getParent()) {
         const auto* record = llvm::dyn_cast<clang::CXXRecordDecl>(context);
         if (record == nullptr) {
            continue;
         }
         const auto contract = annotation(*record, "forge.contract");
         if (contract.has_value()) {
            return contract->empty() ? record->getNameAsString() : *contract;
         }
      }
      return std::nullopt;
   }

   bool belongs_to_selected_contract(const clang::Decl& declaration) const {
      const auto owner = owning_contract(declaration);
      return owner.has_value() && *owner == contract_name_;
   }

   void add_table(const clang::ClassTemplateSpecializationDecl& declaration) {
      const auto* record = specialization_record(declaration);
      if (record == nullptr) {
         return;
      }
      const auto value = declaration.getTemplateArgs()[0].getAsIntegral().getZExtValue();
      encoder_.add_table(*record, protocol::to_string(protocol::name{value}));
   }

   struct method_shape {
      std::string name;
      std::string type;
      std::string result;
   };

   void register_method_types(const clang::CXXMethodDecl& method) {
      if (!method.getReturnType()->isVoidType()) {
         static_cast<void>(encoder_.encode(method.getReturnType()));
      }
      for (const auto* parameter : method.parameters()) {
         static_cast<void>(encoder_.encode(*parameter));
      }
   }

   method_shape add_method(const clang::CXXMethodDecl& method, std::string_view annotated_name,
                           bool allow_unnamed_parameters) {
      if (method.isStatic()) {
         report(method.getLocation(), "contract entry point must be a non-static member function");
      }
      auto shape = method_shape{};
      shape.name = annotated_name.empty() ? method.getNameAsString() : std::string{annotated_name};
      shape.type = shape.name;
      if (!method.getReturnType()->isVoidType()) {
         shape.result = encoder_.encode(method.getReturnType());
      }

      auto arguments = struct_shape{.name = shape.type};
      for (std::size_t index = 0; index < method.getNumParams(); ++index) {
         const auto* parameter = method.getParamDecl(index);
         auto name = parameter->getNameAsString();
         if (name.empty() && !allow_unnamed_parameters) {
            report(parameter->getLocation(), "contract entry point parameters must be named");
            continue;
         }
         arguments.fields.push_back(field_shape{name, encoder_.encode(*parameter)});
      }
      if (claim_struct(output_, context_, arguments.name, "method:" + declaration_identity(method),
                       method.getLocation())) {
         output_.structs.push_back(std::move(arguments));
      }
      return shape;
   }

   void add_action(const clang::CXXRecordDecl& declaration, const clang::CXXMethodDecl& method,
                   std::string_view annotated_name) {
      register_method_types(method);
      const auto identity = declaration_identity(method);
      const auto action_name = annotated_name.empty() ? method.getNameAsString() : std::string{annotated_name};
      const auto [existing, inserted] = output_.action_declarations.try_emplace(action_name, identity);
      if (!inserted && existing->second == identity) {
         return;
      }
      if (!inserted) {
         report(method.getLocation(), "duplicate contract action name");
         return;
      }
      const auto dispatch_name = declaration.getQualifiedNameAsString() + "::" + method.getNameAsString();
      const auto [dispatch, dispatch_inserted] = output_.action_methods.try_emplace(dispatch_name, identity);
      if (!dispatch_inserted && dispatch->second != identity) {
         report(method.getLocation(), "overloaded contract action methods are not supported");
         return;
      }
      const auto method_info =
          add_method(method, annotated_name, has_annotation(method, "forge.attribute_scope:action:eosio"));
      output_.actions.push_back(action_shape{
          .name = method_info.name,
          .type = method_info.type,
          .result = method_info.result,
          .class_name = declaration.getQualifiedNameAsString(),
          .method_name = method.getNameAsString(),
      });
   }

   void add_call(const clang::CXXMethodDecl& method, std::string_view annotated_name) {
      register_method_types(method);
      const auto identity = declaration_identity(method);
      const auto call_name = annotated_name.empty() ? method.getNameAsString() : std::string{annotated_name};
      const auto [existing, inserted] = output_.call_declarations.try_emplace(call_name, identity);
      if (!inserted && existing->second == identity) {
         return;
      }
      if (!inserted) {
         report(method.getLocation(), "duplicate synchronous call name");
         return;
      }
      const auto method_info =
          add_method(method, annotated_name, has_annotation(method, "forge.attribute_scope:call:eosio"));
      auto identifier = std::uint64_t{5381U};
      for (const auto value : method_info.name) {
         identifier = identifier * 33U + static_cast<unsigned char>(value);
      }
      output_.calls.push_back(call_shape{
          .name = method_info.name,
          .type = method_info.type,
          .result = method_info.result,
          .id = identifier,
      });
   }

   void report(clang::SourceLocation location, const char* message) {
      const auto id = context_.getDiagnostics().getCustomDiagID(clang::DiagnosticsEngine::Error, "%0");
      context_.getDiagnostics().Report(location, id) << message;
      output_.failed = true;
   }

   clang::ASTContext& context_;
   schema& output_;
   type_encoder encoder_;
   std::string contract_name_;
   bool found_contract_ = false;
};

class consumer final : public clang::ASTConsumer {
 public:
   consumer(clang::ASTContext& context, schema& output, std::set<std::string>& source_record_codecs, bool& found,
            bool& dispatch_source_found, std::string_view contract_name, bool is_dispatch_source)
       : visitor_(context, output, source_record_codecs, contract_name), found_(found),
         dispatch_source_found_(dispatch_source_found), is_dispatch_source_(is_dispatch_source) {}

   void HandleTranslationUnit(clang::ASTContext& context) override {
      visitor_.TraverseDecl(context.getTranslationUnitDecl());
      const auto found = visitor_.found_contract();
      found_ = found_ || found;
      dispatch_source_found_ = dispatch_source_found_ || (is_dispatch_source_ && found);
   }

 private:
   visitor visitor_;
   bool& found_;
   bool& dispatch_source_found_;
   bool is_dispatch_source_ = false;
};

class action final : public clang::ASTFrontendAction {
 public:
   action(schema& output, bool& found, bool& dispatch_source_found, std::string_view contract_name,
          std::filesystem::path dispatch_source, std::set<std::filesystem::path>& dependencies)
       : output_(output), found_(found), dispatch_source_found_(dispatch_source_found), contract_name_(contract_name),
         dispatch_source_(std::move(dispatch_source)), dependencies_(dependencies) {}

   std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance& compiler,
                                                         llvm::StringRef input_file) override {
      const auto input = std::filesystem::weakly_canonical(std::filesystem::path{input_file.str()});
      return std::make_unique<consumer>(compiler.getASTContext(), output_, output_.source_record_codecs[input], found_,
                                        dispatch_source_found_, contract_name_, input == dispatch_source_);
   }

   void EndSourceFileAction() override {
      const auto& source_manager = getCompilerInstance().getSourceManager();
      for (auto iterator = source_manager.fileinfo_begin(); iterator != source_manager.fileinfo_end(); ++iterator) {
         const auto path = std::filesystem::path{iterator->first.getName().str()};
         if (!path.empty() && std::filesystem::exists(path)) {
            dependencies_.insert(std::filesystem::weakly_canonical(path));
         }
      }
   }

 private:
   schema& output_;
   bool& found_;
   bool& dispatch_source_found_;
   std::string contract_name_;
   std::filesystem::path dispatch_source_;
   std::set<std::filesystem::path>& dependencies_;
};

class action_factory final : public clang::tooling::FrontendActionFactory {
 public:
   action_factory(schema& output, bool& found, bool& dispatch_source_found, std::string_view contract_name,
                  std::filesystem::path dispatch_source, std::set<std::filesystem::path>& dependencies)
       : output_(output), found_(found), dispatch_source_found_(dispatch_source_found), contract_name_(contract_name),
         dispatch_source_(std::move(dispatch_source)), dependencies_(dependencies) {}

   std::unique_ptr<clang::FrontendAction> create() override {
      return std::make_unique<action>(output_, found_, dispatch_source_found_, contract_name_, dispatch_source_,
                                      dependencies_);
   }

 private:
   schema& output_;
   bool& found_;
   bool& dispatch_source_found_;
   std::string contract_name_;
   std::filesystem::path dispatch_source_;
   std::set<std::filesystem::path>& dependencies_;
};

std::string read_text(const std::filesystem::path& path) {
   auto stream = std::ifstream{path, std::ios::binary};
   if (!stream) {
      throw std::runtime_error{"cannot read contract source: " + path.string()};
   }
   return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

void write_text(const std::filesystem::path& path, std::string_view text) {
   if (const auto parent = path.parent_path(); !parent.empty()) {
      std::filesystem::create_directories(parent);
   }
   auto stream = std::ofstream{path, std::ios::binary | std::ios::trunc};
   if (!stream || !(stream << text)) {
      throw std::runtime_error{"cannot write generated contract file: " + path.string()};
   }
}

std::string depfile_path(const std::filesystem::path& path) {
   auto result = std::string{};
   for (const auto value : path.generic_string()) {
      switch (value) {
      case ' ':
      case '#':
         result += '\\';
         result += value;
         break;
      case '$':
         result += "$$";
         break;
      default:
         result += value;
         break;
      }
   }
   return result;
}

void write_depfile(const forge::contract::abi::request& options, const std::set<std::filesystem::path>& dependencies) {
   if (options.depfile.empty()) {
      return;
   }
   auto output = std::ostringstream{};
   output << depfile_path(options.abi) << ':';
   for (const auto& dependency : dependencies) {
      output << " \\\n  " << depfile_path(dependency);
   }
   output << '\n';
   write_text(options.depfile, output.str());
}

std::string trim_lines(std::string value) {
   while (!value.empty() && (value.front() == '\n' || value.front() == '\r')) {
      value.erase(value.begin());
   }
   while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
      value.pop_back();
   }
   return value;
}

std::map<std::string, std::string> read_ricardian_sections(const std::filesystem::path& path,
                                                           std::string_view section_class) {
   if (path.empty()) {
      return {};
   }
   const auto source = read_text(path);
   const auto prefix = std::string{"<h1 class=\""} + std::string{section_class} + "\">";
   constexpr auto suffix = std::string_view{"</h1>"};
   auto result = std::map<std::string, std::string>{};
   auto cursor = std::size_t{0};
   while ((cursor = source.find(prefix, cursor)) != std::string::npos) {
      const auto name_start = cursor + prefix.size();
      const auto name_end = source.find(suffix, name_start);
      if (name_end == std::string::npos) {
         throw std::runtime_error{"malformed Ricardian section header in " + path.string()};
      }
      const auto body_start = name_end + suffix.size();
      const auto next = source.find(prefix, body_start);
      auto name = trim_lines(source.substr(name_start, name_end - name_start));
      auto body = trim_lines(source.substr(body_start, next == std::string::npos ? next : next - body_start));
      if (name.empty() || !result.emplace(std::move(name), std::move(body)).second) {
         throw std::runtime_error{"duplicate or empty Ricardian section in " + path.string()};
      }
      cursor = next;
   }
   return result;
}

void load_ricardian(schema& output, const forge::contract::abi::request& options) {
   const auto clauses = read_ricardian_sections(options.ricardian_clauses, "clause");
   for (const auto& [id, body] : clauses) {
      output.clauses.push_back(protocol::clause_pair{id, body});
   }
}

void canonicalize(schema& output) {
   const auto by_name = [](const auto& left, const auto& right) { return left.name < right.name; };
   std::ranges::sort(output.types, by_name);
   std::ranges::sort(output.structs, by_name);
   std::ranges::sort(output.record_codecs, by_name);
   std::ranges::sort(output.actions, by_name);
   std::ranges::sort(output.variants, by_name);
   std::ranges::sort(output.tables, by_name);
   std::ranges::sort(output.calls, by_name);
   std::ranges::sort(output.clauses, [](const auto& left, const auto& right) { return left.id < right.id; });
}

void write_abi(const schema& input, const forge::contract::abi::request& options) {
   auto abi = protocol::abi_def{};
   abi.version = input.calls.empty() ? "eosio::abi/1.2" : "eosio::abi/1.3";
   for (const auto& source : input.types) {
      abi.types.push_back(protocol::type_def{source.name, source.type});
   }
   for (const auto& source : input.structs) {
      auto target = protocol::struct_def{.name = source.name, .base = source.base};
      for (const auto& field : source.fields) {
         target.fields.push_back(protocol::field_def{field.name, field.type});
      }
      abi.structs.push_back(std::move(target));
   }
   const auto contracts = read_ricardian_sections(options.ricardian_contracts, "contract");
   for (const auto& source : input.actions) {
      const auto found = contracts.find(source.name);
      abi.actions.push_back(protocol::action_def{
          protocol::make_name(source.name),
          source.type,
          found == contracts.end() ? std::string{} : found->second,
      });
      if (!source.result.empty()) {
         abi.action_results.value.push_back(
             protocol::action_result_def{protocol::make_name(source.name), source.result});
      }
   }
   for (const auto& source : input.tables) {
      abi.tables.push_back(protocol::table_def{
          protocol::make_name(source.name),
          "i64",
          {},
          {},
          source.type,
      });
   }
   abi.ricardian_clauses = input.clauses;
   for (const auto& source : input.variants) {
      abi.variants.value.push_back(protocol::variant_def{source.name, source.types});
   }

   auto value = forge::variant{};
   protocol::to_variant(abi, value);
   if (!input.calls.empty()) {
      auto calls = forge::variants{};
      for (const auto& source : input.calls) {
         calls.emplace_back(forge::mutable_variant_object{}("name", source.name)("type", source.type)("id", source.id)(
             "result_type", source.result));
      }
      auto root = forge::mutable_variant_object{value.get_object()};
      root.set("calls", forge::variant{std::move(calls)});
      value = forge::variant{std::move(root)};
   }
   const auto encoded = forge::codec::json::write_value(value, {.pretty = true});
   if (!encoded.ok()) {
      throw std::runtime_error{"failed to encode generated contract ABI"};
   }
   write_text(options.abi, encoded.text + '\n');
}

void write_codec_declarations(std::ostream& output, const schema& input) {
   for (const auto& record : input.record_codecs) {
      if (!record.forward_declaration.empty()) {
         output << record.forward_declaration << '\n';
      }
   }
   for (const auto& record : input.record_codecs) {
      if (record.forward_declaration.empty()) {
         continue;
      }
      output << "template <> struct forge::raw::codec_traits<" << record.name << "> {\n";
      output << "   template <typename Stream> static void pack(Stream& stream, const " << record.name << "& value);\n";
      output << "   template <typename Stream> static void unpack(Stream& stream, " << record.name << "& value);\n";
      output << "};\n";
   }
}

void write_codec_definitions(std::ostream& output, const schema& input,
                             const std::set<std::string>* selected = nullptr) {
   for (const auto& record : input.record_codecs) {
      if (selected != nullptr && !selected->contains(record.name)) {
         continue;
      }
      const auto early_declaration = !record.forward_declaration.empty();
      const auto body_indent = early_declaration ? "   " : "      ";
      if (early_declaration) {
         output << "template <typename Stream> void forge::raw::codec_traits<" << record.name
                << ">::pack(Stream& stream, const " << record.name << "& value) {\n";
      } else {
         output << "template <> struct forge::raw::codec_traits<" << record.name << "> {\n";
         output << "   template <typename Stream> static void pack(Stream& stream, const " << record.name
                << "& value) {\n";
      }
      if (!record.base.empty()) {
         output << body_indent << "forge::raw::pack(stream, static_cast<const " << record.base << "&>(value));\n";
      }
      for (const auto& field : record.fields) {
         output << body_indent << "forge::raw::pack(stream, value." << field << ");\n";
      }
      output << (early_declaration ? "}\n" : "   }\n");

      if (early_declaration) {
         output << "template <typename Stream> void forge::raw::codec_traits<" << record.name
                << ">::unpack(Stream& stream, " << record.name << "& value) {\n";
      } else {
         output << "   template <typename Stream> static void unpack(Stream& stream, " << record.name << "& value) {\n";
      }
      if (!record.base.empty()) {
         output << body_indent << "forge::raw::unpack(stream, static_cast<" << record.base << "&>(value));\n";
      }
      for (const auto& field : record.fields) {
         output << body_indent << "forge::raw::unpack(stream, value." << field << ");\n";
      }
      output << (early_declaration ? "}\n" : "   }\n};\n");
   }
}

const std::set<std::string>& source_record_codecs(const schema& input, const std::filesystem::path& source) {
   static const auto empty = std::set<std::string>{};
   const auto canonical = std::filesystem::weakly_canonical(source);
   const auto found = input.source_record_codecs.find(canonical);
   return found == input.source_record_codecs.end() ? empty : found->second;
}

std::string make_codec_prelude(const schema& input) {
   if (input.has_apply && !input.has_eosio_dispatch) {
      return {};
   }
   auto output = std::ostringstream{};
   output << "#pragma once\n";
   output << "import forge.raw.codec;\n";
   write_codec_declarations(output, input);
   return output.str();
}

void write_dispatcher(const schema& input, const forge::contract::abi::request& options) {
   auto output = std::ostringstream{};
   if (!input.has_apply || input.has_eosio_dispatch) {
      output << "#include <cstdint>\n";
      output << "import forge.contract.dispatcher;\n";
   } else {
      output << "import forge.raw.codec;\n";
   }
   write_codec_declarations(output, input);
   if (input.has_eosio_dispatch) {
      output << "#define FORGE_CONTRACT_DEFER_EOSIO_DISPATCH 1\n";
   }
   const auto source = std::filesystem::weakly_canonical(options.sources.front()).generic_string();
   output << "#include " << std::quoted(source) << "\n";
   if (input.has_eosio_dispatch) {
      output << "#undef FORGE_CONTRACT_DEFER_EOSIO_DISPATCH\n";
   }
   if (input.has_apply && !input.has_eosio_dispatch) {
      output << "#line 1 \"contract generated codecs\"\n";
      write_codec_definitions(output, input, &source_record_codecs(input, options.sources.front()));
      write_text(options.dispatcher, output.str());
      return;
   }
   output << "#line 1 \"contract generated dispatcher\"\n";
   write_codec_definitions(output, input, &source_record_codecs(input, options.sources.front()));
   if (input.has_eosio_dispatch) {
      output << "extern \"C\" [[gnu::visibility(\"default\")]] void apply("
                "std::uint64_t receiver, std::uint64_t code, std::uint64_t action) {\n";
      output << "   forge::contract::detail::eosio_dispatch_definition<void>::invoke(receiver, code, action);\n";
      output << "}\n";
      write_text(options.dispatcher, output.str());
      return;
   }
   output << "extern \"C\" [[gnu::visibility(\"default\")]] void apply("
             "std::uint64_t receiver, std::uint64_t code, std::uint64_t action) {\n";
   output << "   using forge::chain::protocol::name;\n";
   if (!input.actions.empty()) {
      output << "   static constexpr forge::contract::dispatch_entry entries[] = {\n";
      for (const auto& entry : input.actions) {
         output << "      forge::contract::make_dispatch_entry<" << entry.class_name << ", &" << entry.class_name
                << "::" << entry.method_name << ">(" << protocol::make_name(entry.name).value << "ULL),\n";
      }
      output << "   };\n";
      output << "   forge::contract::dispatch(name{receiver}, name{code}, action, entries);\n";
   } else {
      output << "   forge::contract::dispatch(name{receiver}, name{code}, action);\n";
   }
   output << "}\n";
   write_text(options.dispatcher, output.str());
}

void write_source_wrappers(const schema& input, const forge::contract::abi::request& options) {
   if (options.source_wrappers.empty()) {
      return;
   }
   if (options.source_wrappers.size() + 1U != options.sources.size()) {
      throw std::runtime_error{"source wrapper count does not match non-dispatch contract sources"};
   }
   for (auto index = std::size_t{0}; index < options.source_wrappers.size(); ++index) {
      const auto& source = options.sources[index + 1U];
      auto output = std::ostringstream{};
      output << "import forge.raw.codec;\n";
      write_codec_declarations(output, input);
      output << "#include " << std::quoted(std::filesystem::weakly_canonical(source).generic_string()) << "\n";
      output << "#line 1 \"contract generated codecs\"\n";
      write_codec_definitions(output, input, &source_record_codecs(input, source));
      write_text(options.source_wrappers[index], output.str());
   }
}

} // namespace

namespace forge::contract::abi {

artifacts generate(const request& options) {
   auto load_error = std::string{};
   if (llvm::sys::DynamicLibrary::LoadLibraryPermanently(options.attribute_plugin.c_str(), &load_error)) {
      throw std::runtime_error{"failed to load contract attribute plugin: " + load_error};
   }

   auto arguments = std::vector<std::string>{
       "-std=c++23",
       "--target=wasm32",
       "-fsyntax-only",
       "-fno-exceptions",
       "-fno-rtti",
       "-ffreestanding",
       "--sysroot=" + options.sysroot.string(),
       "-mcpu=mvp",
       "-DFORGE_CONTRACT_GUEST=1",
       "-DFORGE_CONTRACT_DEFER_EOSIO_DISPATCH=1",
   };
   for (const auto& path : options.include_paths) {
      arguments.push_back("-I" + path.string());
   }
   for (const auto& module : options.module_files) {
      arguments.push_back("-fmodule-file=" + module);
   }

   auto source_paths = std::vector<std::string>{};
   source_paths.reserve(options.sources.size());
   for (const auto& source : options.sources) {
      source_paths.push_back(source.string());
   }

   // Discover namespace-scope ABI records before the strict pass so contract code can use their generated codecs.
   auto discovery_compilation = clang::tooling::FixedCompilationDatabase{".", arguments};
   auto discovery_tool = clang::tooling::ClangTool{discovery_compilation, source_paths};
   auto discovery = schema{};
   auto discovery_found = false;
   auto discovery_dispatch_source_found = false;
   auto discovery_dependencies = std::set<std::filesystem::path>{};
   const auto dispatch_source = std::filesystem::weakly_canonical(options.sources.front());
   auto discovery_factory = action_factory{discovery,        discovery_found, discovery_dispatch_source_found,
                                           options.contract, dispatch_source, discovery_dependencies};
   auto ignored_diagnostics = clang::IgnoringDiagConsumer{};
   discovery_tool.setDiagnosticConsumer(&ignored_diagnostics);
   static_cast<void>(discovery_tool.run(&discovery_factory));

   const auto codec_prelude = make_codec_prelude(discovery);
   constexpr auto codec_prelude_path = "/__forge_contract_generated_codecs.hpp";
   if (!codec_prelude.empty()) {
      arguments.push_back("-include");
      arguments.push_back(codec_prelude_path);
   }

   auto compilation = clang::tooling::FixedCompilationDatabase{".", arguments};
   auto tool = clang::tooling::ClangTool{compilation, source_paths};
   if (!codec_prelude.empty()) {
      tool.mapVirtualFile(codec_prelude_path, codec_prelude);
   }
   auto output = schema{};
   auto found = false;
   auto dispatch_source_found = false;
   auto dependencies = std::set<std::filesystem::path>{};
   auto factory = action_factory{output, found, dispatch_source_found, options.contract, dispatch_source, dependencies};
   const auto result = tool.run(&factory);
   if (result != 0 || output.failed) {
      throw std::runtime_error{"contract source analysis failed"};
   }
   if (!found) {
      throw std::runtime_error{"contract '" + options.contract + "' was not found"};
   }
   if (!dispatch_source_found) {
      throw std::runtime_error{"first contract source must declare contract '" + options.contract +
                               "': " + dispatch_source.string()};
   }
   if (output.actions.empty() && output.calls.empty() && output.tables.empty()) {
      throw std::runtime_error{"contract '" + options.contract + "' has no ABI entries"};
   }

   load_ricardian(output, options);
   canonicalize(output);
   write_abi(output, options);
   write_dispatcher(output, options);
   write_source_wrappers(output, options);
   write_depfile(options, dependencies);
   return {.abi = options.abi, .dispatcher = options.dispatcher, .source_wrappers = options.source_wrappers};
}

} // namespace forge::contract::abi
