module;

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <forge/exceptions/macros.hpp>
#include <iterator>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

module forge.contract.graph;

import forge.codec.json;
import forge.contract.graph.exceptions;
import forge.variant.value;

namespace forge::contract::graph {
namespace {

using forge::variant;
using forge::variant_object;
using forge::variants;

std::string read_text(const std::filesystem::path& path) {
   auto input = std::ifstream{path, std::ios::binary};
   if (!input) {
      FORGE_THROW_EXCEPTION(exceptions::read_error, "cannot read contract graph",
                            forge::exceptions::ctx("path", path.string()));
   }
   return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

const variant_object& require_object(const variant& value, std::string_view description) {
   if (!value.is_object()) {
      throw std::runtime_error{std::string{description} + " must be an object"};
   }
   return value.get_object();
}

const variants& require_array(const variant_object& object, const char* name, std::string_view description) {
   if (!object.contains(name) || !object[name].is_array()) {
      throw std::runtime_error{std::string{description} + " has no " + name + " array"};
   }
   return object[name].get_array();
}

std::string require_string(const variant_object& object, const char* name, std::string_view description) {
   if (!object.contains(name) || !object[name].is_string() || object[name].get_string().empty()) {
      throw std::runtime_error{std::string{description} + " has no " + name};
   }
   return object[name].get_string();
}

bool is_portable(const std::filesystem::path& path) {
   if (path.empty() || path.is_absolute()) {
      return false;
   }
   return std::ranges::none_of(
       path, [](const auto& component) { return component.empty() || component == "." || component == ".."; });
}

bool is_under(const std::filesystem::path& path, const std::filesystem::path& root) {
   const auto relative = path.lexically_relative(root);
   return !relative.empty() && !relative.is_absolute() &&
          std::ranges::none_of(relative, [](const auto& component) { return component == ".."; });
}

file_role parse_role(std::string_view value, bool root) {
   if (root) {
      if (value == "dispatch_source") {
         return file_role::dispatch_source;
      }
      if (value == "source") {
         return file_role::source;
      }
      if (value == "header") {
         return file_role::header;
      }
      if (value == "compile_check") {
         return file_role::compile_check;
      }
      if (value == "ricardian_contracts") {
         return file_role::ricardian_contracts;
      }
      if (value == "ricardian_clauses") {
         return file_role::ricardian_clauses;
      }
   } else {
      if (value == "module") {
         return file_role::module;
      }
      if (value == "implementation") {
         return file_role::implementation;
      }
      if (value == "public_header") {
         return file_role::public_header;
      }
      if (value == "private_header") {
         return file_role::private_header;
      }
   }
   throw std::runtime_error{"contract graph has an invalid file role: " + std::string{value}};
}

dependency_kind parse_kind(std::string_view value) {
   if (value == "library") {
      return dependency_kind::library;
   }
   if (value == "component") {
      return dependency_kind::component;
   }
   throw std::runtime_error{"contract graph has an invalid dependency kind: " + std::string{value}};
}

dependency_scope parse_scope(std::string_view value) {
   if (value == "public") {
      return dependency_scope::public_;
   }
   if (value == "private") {
      return dependency_scope::private_;
   }
   throw std::runtime_error{"contract graph has an invalid dependency scope: " + std::string{value}};
}

class builder {
 public:
   void add_owner(std::string owner, const variants& roots, const variants& files, bool root) {
      if (component_ids_.contains(owner)) {
         throw std::runtime_error{"contract graph ID is shared by a library and component: " + owner};
      }
      if (!owners_.insert(owner).second) {
         throw std::runtime_error{"contract graph contains a duplicate owner: " + owner};
      }

      auto owner_roots = std::vector<std::filesystem::path>{};
      owner_roots.reserve(roots.size());
      for (const auto& value : roots) {
         if (!value.is_string() || value.get_string().empty()) {
            throw std::runtime_error{"contract graph contains an invalid source root"};
         }
         const auto path = std::filesystem::weakly_canonical(value.get_string());
         if (!std::filesystem::is_directory(path)) {
            throw std::runtime_error{"contract graph source root is not a directory: " + path.string()};
         }
         if (std::ranges::find(owner_roots, path) != owner_roots.end()) {
            throw std::runtime_error{"contract graph contains a duplicate source root: " + path.string()};
         }
         owner_roots.push_back(path);
         result_.source_roots.push_back(source_root{
             .owner = owner,
             .physical_path = path,
         });
      }
      if (owner_roots.empty()) {
         throw std::runtime_error{"contract graph owner has no source root: " + owner};
      }

      for (const auto& value : files) {
         const auto& object = require_object(value, "contract graph file");
         const auto role = parse_role(require_string(object, "role", "contract graph file"), root);
         const auto logical =
             std::filesystem::path{require_string(object, "logical_path", "contract graph file")}.lexically_normal();
         if (!is_portable(logical)) {
            throw std::runtime_error{"contract graph has a non-portable logical path: " + logical.string()};
         }
         const auto physical =
             std::filesystem::weakly_canonical(require_string(object, "physical_path", "contract graph file"));
         if (!std::filesystem::is_regular_file(physical)) {
            throw std::runtime_error{"contract graph input is not a file: " + physical.string()};
         }
         if (!std::ranges::any_of(owner_roots,
                                  [&](const auto& source_root) { return is_under(physical, source_root); })) {
            throw std::runtime_error{"contract graph input is outside its owner source roots: " + physical.string()};
         }
         if (!logical_files_.emplace(owner, logical).second) {
            throw std::runtime_error{"contract graph contains a duplicate logical path: " + logical.string()};
         }
         if (!physical_files_.insert(physical).second) {
            throw std::runtime_error{"contract graph gives one file multiple identities: " + physical.string()};
         }
         result_.files.push_back(file{
             .owner = owner,
             .role = role,
             .logical_path = logical,
             .physical_path = physical,
         });
      }
      if (files.empty()) {
         throw std::runtime_error{"contract graph owner has no files: " + owner};
      }
   }

   void add_dependency(std::string owner, dependency_kind kind, std::string target, dependency_scope scope) {
      const auto key = std::tuple{owner, kind, target};
      if (!dependencies_.insert(key).second) {
         throw std::runtime_error{"contract graph contains a duplicate dependency"};
      }
      result_.dependencies.push_back(dependency{
          .owner = std::move(owner),
          .kind = kind,
          .target = std::move(target),
          .scope = scope,
      });
   }

   void add_component(std::string id, const variants& modules, const variants& dependencies) {
      if (owners_.contains(id)) {
         throw std::runtime_error{"contract graph ID is shared by a library and component: " + id};
      }
      if (!component_ids_.insert(id).second) {
         throw std::runtime_error{"contract graph contains a duplicate component ID: " + id};
      }
      auto value = component{.id = id};
      value.modules.reserve(modules.size());
      for (const auto& module : modules) {
         if (!module.is_string() || module.get_string().empty() ||
             !module_owners_.emplace(module.get_string(), id).second) {
            throw std::runtime_error{"contract graph component has an invalid or duplicate module"};
         }
         value.modules.push_back(module.get_string());
      }
      if (value.modules.empty()) {
         throw std::runtime_error{"contract graph component has no modules: " + id};
      }
      result_.components.push_back(std::move(value));
      for (const auto& dependency : dependencies) {
         if (!dependency.is_string() || dependency.get_string().empty()) {
            throw std::runtime_error{"contract graph component has an invalid dependency"};
         }
         add_dependency(id, dependency_kind::component, dependency.get_string(), dependency_scope::public_);
      }
   }

   descriptor finish(std::string root_owner) {
      for (const auto& edge : result_.dependencies) {
         if (!owners_.contains(edge.owner) && !component_ids_.contains(edge.owner)) {
            throw std::runtime_error{"contract graph dependency has an unknown owner: " + edge.owner};
         }
         if (edge.kind == dependency_kind::library && !owners_.contains(edge.target)) {
            throw std::runtime_error{"contract graph dependency has an unknown library: " + edge.target};
         }
         if (edge.kind == dependency_kind::component && !component_ids_.contains(edge.target)) {
            throw std::runtime_error{"contract graph dependency has an unknown component: " + edge.target};
         }
      }

      auto visiting = std::set<std::string>{};
      auto visited = std::set<std::string>{};
      const auto visit = [&](auto&& self, const std::string& owner) -> void {
         if (visited.contains(owner)) {
            return;
         }
         if (!visiting.insert(owner).second) {
            throw std::runtime_error{"contract graph contains a library dependency cycle: " + owner};
         }
         for (const auto& edge : result_.dependencies) {
            if (edge.owner == owner && edge.kind == dependency_kind::library) {
               self(self, edge.target);
            }
         }
         visiting.erase(owner);
         visited.insert(owner);
      };
      for (const auto& owner : owners_) {
         visit(visit, owner);
      }

      visiting.clear();
      visited.clear();
      const auto visit_component = [&](auto&& self, const std::string& owner) -> void {
         if (visited.contains(owner)) {
            return;
         }
         if (!visiting.insert(owner).second) {
            throw std::runtime_error{"contract graph contains a component dependency cycle: " + owner};
         }
         for (const auto& edge : result_.dependencies) {
            if (edge.owner == owner && edge.kind == dependency_kind::component) {
               self(self, edge.target);
            }
         }
         visiting.erase(owner);
         visited.insert(owner);
      };
      for (const auto& component : component_ids_) {
         visit_component(visit_component, component);
      }

      result_.root_owner = std::move(root_owner);
      return std::move(result_);
   }

 private:
   descriptor result_;
   std::set<std::string> owners_;
   std::set<std::string> component_ids_;
   std::map<std::string, std::string> module_owners_;
   std::set<std::pair<std::string, std::filesystem::path>> logical_files_;
   std::set<std::filesystem::path> physical_files_;
   std::set<std::tuple<std::string, dependency_kind, std::string>> dependencies_;
};

} // namespace

descriptor read_impl(const std::filesystem::path& path) {
   const auto parsed = forge::codec::json::read_value(read_text(path), {.source_name = path.string()});
   if (!parsed.ok() || !parsed.value.is_object()) {
      throw std::runtime_error{"contract graph is not a valid JSON object"};
   }
   const auto& document = parsed.value.get_object();
   if (!document.contains("schema") || document["schema"].as_uint64() != 2U) {
      throw std::runtime_error{"contract graph has an unsupported schema"};
   }

   auto result = builder{};
   const auto& root = require_object(document["root"], "contract graph root");
   const auto root_owner = require_string(root, "owner", "contract graph root");
   const auto root_path = require_string(root, "source_root", "contract graph root");
   result.add_owner(root_owner, variants{variant{root_path}}, require_array(root, "files", "contract graph root"),
                    true);
   for (const auto& value : require_array(root, "libraries", "contract graph root")) {
      if (!value.is_string() || value.get_string().empty()) {
         throw std::runtime_error{"contract graph root has an invalid library dependency"};
      }
      result.add_dependency(root_owner, dependency_kind::library, value.get_string(), dependency_scope::public_);
   }
   for (const auto& value : require_array(root, "components", "contract graph root")) {
      if (!value.is_string() || value.get_string().empty()) {
         throw std::runtime_error{"contract graph root has an invalid component dependency"};
      }
      result.add_dependency(root_owner, dependency_kind::component, value.get_string(), dependency_scope::public_);
   }

   for (const auto& value : require_array(document, "libraries", "contract graph")) {
      const auto& library = require_object(value, "contract graph library");
      const auto owner = require_string(library, "id", "contract graph library");
      result.add_owner(owner, require_array(library, "source_roots", "contract graph library"),
                       require_array(library, "files", "contract graph library"), false);
      for (const auto& edge_value : require_array(library, "dependencies", "contract graph library")) {
         const auto& edge = require_object(edge_value, "contract graph dependency");
         result.add_dependency(owner, parse_kind(require_string(edge, "kind", "contract graph dependency")),
                               require_string(edge, "id", "contract graph dependency"),
                               parse_scope(require_string(edge, "scope", "contract graph dependency")));
      }
   }
   for (const auto& value : require_array(document, "components", "contract graph")) {
      const auto& component = require_object(value, "contract graph component");
      result.add_component(require_string(component, "id", "contract graph component"),
                           require_array(component, "modules", "contract graph component"),
                           require_array(component, "dependencies", "contract graph component"));
   }
   return result.finish(root_owner);
}

descriptor read(const std::filesystem::path& path) {
   try {
      return read_impl(path);
   } catch (const exceptions::read_error&) {
      throw;
   } catch (const std::exception& error) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_descriptor, "invalid contract graph descriptor",
                            forge::exceptions::ctx("path", path.string()),
                            forge::exceptions::ctx("cause", error.what()));
   }
}

bool is_public(file_role value) {
   return value == file_role::module || value == file_role::public_header;
}

std::string to_string(file_role value) {
   switch (value) {
   case file_role::dispatch_source:
      return "dispatch_source";
   case file_role::source:
      return "source";
   case file_role::header:
      return "header";
   case file_role::compile_check:
      return "compile_check";
   case file_role::ricardian_contracts:
      return "ricardian_contracts";
   case file_role::ricardian_clauses:
      return "ricardian_clauses";
   case file_role::module:
      return "module";
   case file_role::implementation:
      return "implementation";
   case file_role::public_header:
      return "public_header";
   case file_role::private_header:
      return "private_header";
   }
   throw std::runtime_error{"invalid contract graph file role"};
}

std::string to_string(dependency_kind value) {
   return value == dependency_kind::library ? "library" : "component";
}

std::string to_string(dependency_scope value) {
   return value == dependency_scope::public_ ? "public" : "private";
}

} // namespace forge::contract::graph
