module;

#include <clang/AST/Attr.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/TypeLoc.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/Tooling.h>
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
   std::vector<action_shape> actions;
   std::vector<variant_shape> variants;
   std::vector<table_shape> tables;
   std::vector<protocol::clause_pair> clauses;
   std::vector<call_shape> calls;
   std::set<std::string> type_names;
   std::set<std::string> struct_names;
   std::set<std::string> variant_names;
   std::set<std::string> table_names;
   std::set<std::string> action_names;
   std::set<std::string> call_names;
   bool has_apply = false;
   bool failed = false;
};

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
   type_encoder(clang::ASTContext& context, schema& output) : context_(context), output_(output) {}

   std::string encode(clang::QualType input) {
      auto type = input.getNonReferenceType().getUnqualifiedType();
      if (const auto* alias = llvm::dyn_cast_or_null<clang::TypedefType>(type.getTypePtrOrNull())) {
         const auto* declaration = alias->getDecl();
         if (!context_.getSourceManager().isInSystemHeader(declaration->getLocation())) {
            const auto name = declaration->getNameAsString();
            const auto target = encode(declaration->getUnderlyingType());
            add_alias(name, target);
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

   void add_table(const clang::CXXRecordDecl& declaration, std::string name) {
      if (name.empty()) {
         name = record_name(declaration);
      }
      if (!output_.table_names.insert(name).second) {
         return;
      }
      output_.tables.push_back(table_shape{std::move(name), encode_record(declaration)});
   }

 private:
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
      const auto name = declaration->getNameAsString();
      const auto type_argument = [&](std::size_t index) -> clang::TypeLoc {
         const auto& argument = specialization.getArgLoc(static_cast<unsigned>(index));
         const auto* source = argument.getTypeSourceInfo();
         return source == nullptr ? clang::TypeLoc{} : source->getTypeLoc();
      };
      if ((name == "vector" || name == "set" || name == "deque" || name == "list") &&
          specialization.getNumArgs() >= 1U) {
         return encode_location(type_argument(0)) + "[]";
      }
      if (name == "optional" && specialization.getNumArgs() >= 1U) {
         return encode_location(type_argument(0)) + '?';
      }
      if (name == "array" && specialization.getNumArgs() >= 2U) {
         return encode_location(type_argument(0)) + '[' +
                std::to_string(integral_argument(specialization.getArgLoc(1))) + ']';
      }
      if ((name == "pair" || name == "map") && specialization.getNumArgs() >= 2U) {
         const auto first = type_argument(0);
         const auto second = type_argument(1);
         const auto pair = add_pair(template_part_location(first), template_part_location(second));
         return name == "map" ? pair + "[]" : pair;
      }
      if (name == "tuple") {
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
      if (name == "variant") {
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
      if (declaration == nullptr ||
          (declaration->getIdentifier() != nullptr && declaration->getIdentifier()->getName() == "basic_string")) {
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
      if (qualified == "std::basic_string<char>" || qualified.find("basic_string") != std::string::npos) {
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

      if (const auto* specialization = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(record)) {
         const auto template_name = specialization->getSpecializedTemplate()->getNameAsString();
         const auto arguments = flatten(specialization->getTemplateArgs());
         if ((template_name == "vector" || template_name == "set" || template_name == "deque" ||
              template_name == "list") &&
             arguments.size() >= 1U && arguments[0].getKind() == clang::TemplateArgument::Type) {
            return encode(arguments[0].getAsType()) + "[]";
         }
         if (template_name == "optional" && arguments.size() >= 1U &&
             arguments[0].getKind() == clang::TemplateArgument::Type) {
            return encode(arguments[0].getAsType()) + '?';
         }
         if (template_name == "array" && arguments.size() >= 2U &&
             arguments[0].getKind() == clang::TemplateArgument::Type &&
             arguments[1].getKind() == clang::TemplateArgument::Integral) {
            return encode(arguments[0].getAsType()) + '[' +
                   std::to_string(arguments[1].getAsIntegral().getZExtValue()) + ']';
         }
         if (template_name == "pair" && arguments.size() >= 2U) {
            return add_pair(arguments[0].getAsType(), arguments[1].getAsType());
         }
         if (template_name == "map" && arguments.size() >= 2U) {
            return add_pair(arguments[0].getAsType(), arguments[1].getAsType()) + "[]";
         }
         if (template_name == "tuple") {
            return add_tuple(arguments);
         }
         if (template_name == "variant") {
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
      const auto name = specialization->getSpecializedTemplate()->getNameAsString();
      if (name == "basic_string") {
         return encoded;
      }

      auto result = std::string{"B_"} + name;
      const auto arguments = flatten(specialization->getTemplateArgs());
      const auto semantic_arguments = name == "map" || name == "pair" ? std::min<std::size_t>(2U, arguments.size())
                                      : name == "array"               ? std::min<std::size_t>(2U, arguments.size())
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

   void add_alias(const std::string& name, const std::string& target) {
      if (!name.empty() && name != target && output_.type_names.insert(name).second) {
         output_.types.push_back(type_shape{name, target});
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
      if (!output_.struct_names.insert(name).second) {
         return;
      }
      output_.structs.push_back(struct_shape{.name = std::move(name), .fields = std::move(fields)});
   }

   void add_struct(const clang::RecordDecl& declaration, const std::string& name) {
      if (!output_.struct_names.insert(name).second) {
         return;
      }

      auto shape = struct_shape{.name = name};
      const auto* record = &declaration;
      if (const auto* cpp = llvm::dyn_cast<clang::CXXRecordDecl>(record); cpp != nullptr) {
         if (const auto* definition = cpp->getDefinition(); definition != nullptr) {
            record = definition;
         }
      }
      if (const auto* cpp = llvm::dyn_cast<clang::CXXRecordDecl>(record); cpp != nullptr && cpp->hasDefinition()) {
         if (cpp->getNumBases() > 1U) {
            fail("multiple ABI base classes", declaration.getLocation());
         } else if (cpp->getNumBases() == 1U) {
            shape.base = encode(cpp->bases_begin()->getType());
         }
      }
      for (const auto* field : record->fields()) {
         shape.fields.push_back(field_shape{field->getNameAsString(), encode(*field)});
      }
      output_.structs.push_back(std::move(shape));
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
};

class visitor final : public clang::RecursiveASTVisitor<visitor> {
 public:
   visitor(clang::ASTContext& context, schema& output, std::string_view contract_name)
       : context_(context), output_(output), encoder_(context, output), contract_name_(contract_name) {}

   bool VisitCXXRecordDecl(clang::CXXRecordDecl* declaration) {
      if (!declaration->isThisDeclarationADefinition() || declaration->isImplicit()) {
         return true;
      }
      if (const auto table = annotation(*declaration, "forge.table"); table.has_value()) {
         if (!table->empty()) {
            encoder_.add_table(*declaration, *table);
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
      if (!declaration->isThisDeclarationADefinition() || !is_global_function(*declaration) ||
          declaration->getIdentifier() == nullptr || declaration->getIdentifier()->getName() != "apply") {
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
      return true;
   }

   bool VisitClassTemplateSpecializationDecl(clang::ClassTemplateSpecializationDecl* declaration) {
      const auto* record = specialization_record(*declaration);
      if (record == nullptr || annotation(*record, "forge.contract") != std::optional<std::string>{contract_name_}) {
         return true;
      }
      add_table(*declaration);
      return true;
   }

   bool VisitTypedefNameDecl(clang::TypedefNameDecl* declaration) {
      const auto* specialization = specialization_type(declaration->getUnderlyingType());
      const auto* record = specialization == nullptr ? nullptr : specialization_record(*specialization);
      const auto owned_record =
          record != nullptr && annotation(*record, "forge.contract") == std::optional<std::string>{contract_name_};
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

   bool belongs_to_selected_contract(const clang::Decl& declaration) const {
      for (auto* context = declaration.getDeclContext(); context != nullptr; context = context->getParent()) {
         const auto* record = llvm::dyn_cast<clang::CXXRecordDecl>(context);
         if (record == nullptr) {
            continue;
         }
         const auto contract = annotation(*record, "forge.contract");
         if (contract.has_value()) {
            const auto declared_name = contract->empty() ? record->getNameAsString() : *contract;
            return declared_name == contract_name_;
         }
      }
      return false;
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

   method_shape add_method(const clang::CXXMethodDecl& method, std::string_view annotated_name) {
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
         arguments.fields.push_back(field_shape{name, encoder_.encode(*parameter)});
      }
      if (output_.struct_names.insert(arguments.name).second) {
         output_.structs.push_back(std::move(arguments));
      }
      return shape;
   }

   void add_action(const clang::CXXRecordDecl& declaration, const clang::CXXMethodDecl& method,
                   std::string_view annotated_name) {
      const auto method_info = add_method(method, annotated_name);
      if (!output_.action_names.insert(method_info.name).second) {
         report(method.getLocation(), "duplicate contract action name");
         return;
      }
      output_.actions.push_back(action_shape{
          .name = method_info.name,
          .type = method_info.type,
          .result = method_info.result,
          .class_name = declaration.getQualifiedNameAsString(),
          .method_name = method.getNameAsString(),
      });
   }

   void add_call(const clang::CXXMethodDecl& method, std::string_view annotated_name) {
      const auto method_info = add_method(method, annotated_name);
      if (!output_.call_names.insert(method_info.name).second) {
         report(method.getLocation(), "duplicate synchronous call name");
         return;
      }
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
   consumer(clang::ASTContext& context, schema& output, bool& found, bool& dispatch_source_found,
            std::string_view contract_name, bool is_dispatch_source)
       : visitor_(context, output, contract_name), found_(found), dispatch_source_found_(dispatch_source_found),
         is_dispatch_source_(is_dispatch_source) {}

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
      return std::make_unique<consumer>(compiler.getASTContext(), output_, found_, dispatch_source_found_,
                                        contract_name_, input == dispatch_source_);
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
   std::filesystem::create_directories(path.parent_path());
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

void write_dispatcher(const schema& input, const forge::contract::abi::request& options) {
   auto output = std::ostringstream{};
   if (!input.has_apply) {
      output << "import forge.contract.dispatcher;\n";
   }
   const auto source = std::filesystem::weakly_canonical(options.sources.front()).generic_string();
   output << "#include " << std::quoted(source) << "\n";
   if (input.has_apply) {
      write_text(options.dispatcher, output.str());
      return;
   }
   output << "#line 1 \"contract generated dispatcher\"\n";
   output << "extern \"C\" [[gnu::visibility(\"default\")]] void apply("
             "std::uint64_t receiver, std::uint64_t code, std::uint64_t action) {\n";
   output << "   using forge::chain::protocol::name;\n";
   output << "   if (code != receiver) {\n";
   output << "      return;\n";
   output << "   }\n";
   output << "   switch (action) {\n";
   for (const auto& entry : input.actions) {
      output << "      case " << protocol::make_name(entry.name).value << "ULL:\n";
      output << "         forge::contract::execute_action<" << entry.class_name << ">(";
      output << "name{receiver}, name{code}, &" << entry.class_name << "::" << entry.method_name << ");\n";
      output << "         return;\n";
   }
   output << "      default:\n";
   output << "         return;\n";
   output << "   }\n";
   output << "}\n";
   write_text(options.dispatcher, output.str());
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
   auto compilation = clang::tooling::FixedCompilationDatabase{".", arguments};
   auto tool = clang::tooling::ClangTool{compilation, source_paths};
   auto output = schema{};
   auto found = false;
   auto dispatch_source_found = false;
   auto dependencies = std::set<std::filesystem::path>{};
   const auto dispatch_source = std::filesystem::weakly_canonical(options.sources.front());
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
   write_depfile(options, dependencies);
   return {.abi = options.abi, .dispatcher = options.dispatcher};
}

} // namespace forge::contract::abi
