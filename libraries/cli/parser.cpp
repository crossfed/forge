module;

#include <CLI/CLI.hpp>
#include <boost/charconv/from_chars.hpp>
#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

module forge.cli.parser;

import forge.cli.command;
import forge.cli.exceptions;

namespace forge::cli {
namespace {

struct option_state {
   const option* specification = nullptr;
   std::size_t occurrences = 0;
   std::vector<std::string> raw_values;
};

struct argument_state {
   const argument* specification = nullptr;
   std::vector<std::string> raw_values;
};

enum class floating_parse_result : std::uint8_t {
   success,
   invalid,
   out_of_range,
   non_finite,
};

struct backend_node {
   CLI::App* backend = nullptr;
   backend_node* parent = nullptr;
   const command* specification = nullptr;
   std::vector<std::string> path;
   std::vector<std::shared_ptr<option_state>> local_options;
   std::vector<std::shared_ptr<argument_state>> arguments;
   std::vector<std::unique_ptr<backend_node>> children;
};

bool is_kebab_name(std::string_view value) {
   if (value.empty() || value.front() == '-' || value.back() == '-') {
      return false;
   }
   auto previous_dash = false;
   for (const auto character : value) {
      const auto valid =
          (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '-';
      if (!valid || (character == '-' && previous_dash)) {
         return false;
      }
      previous_dash = character == '-';
   }
   return true;
}

bool is_application_name(std::string_view value) {
   if (value.empty() || value.front() == '-' || value.front() == '_' || value.back() == '-' || value.back() == '_') {
      return false;
   }
   auto previous_separator = false;
   for (const auto character : value) {
      const auto separator = character == '-' || character == '_';
      const auto valid = (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || separator;
      if (!valid || (separator && previous_separator)) {
         return false;
      }
      previous_separator = separator;
   }
   return true;
}

bool is_option_alias(std::string_view value) {
   if (value.size() == 2 && value[0] == '-' && value[1] != '-') {
      return std::isalnum(static_cast<unsigned char>(value[1])) != 0;
   }
   return value.starts_with("--") && is_kebab_name(value.substr(2));
}

std::string command_location(std::span<const std::string> path) {
   if (path.empty()) {
      return "<root>";
   }
   auto result = std::string{};
   for (const auto& item : path) {
      if (!result.empty()) {
         result += ' ';
      }
      result += item;
   }
   return result;
}

std::vector<std::string> option_spellings(const option& value) {
   auto result = std::vector<std::string>{"--" + value.name};
   result.insert(result.end(), value.aliases.begin(), value.aliases.end());
   return result;
}

std::string join_spellings(const option& value) {
   const auto spellings = option_spellings(value);
   auto result = std::string{};
   for (const auto& spelling : spellings) {
      if (!result.empty()) {
         result += ',';
      }
      result += spelling;
   }
   return result;
}

std::string type_name(value_kind type) {
   switch (type) {
   case value_kind::text:
      return "TEXT";
   case value_kind::integer:
      return "INTEGER";
   case value_kind::real:
      return "NUMBER";
   case value_kind::boolean:
      return "BOOLEAN";
   }
   return "VALUE";
}

std::optional<bool> parse_boolean(std::string_view value) {
   auto normalized = std::string{value};
   std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                  [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
   if (normalized == "true" || normalized == "1" || normalized == "yes" || normalized == "on") {
      return true;
   }
   if (normalized == "false" || normalized == "0" || normalized == "no" || normalized == "off") {
      return false;
   }
   return std::nullopt;
}

floating_parse_result parse_floating(std::string_view input, double& value) noexcept {
   const auto result = boost::charconv::from_chars(input.data(), input.data() + input.size(), value,
                                                   boost::charconv::chars_format::general);
   if (result.ec == std::errc::result_out_of_range) {
      return floating_parse_result::out_of_range;
   }
   if (result.ec != std::errc{} || result.ptr != input.data() + input.size()) {
      return floating_parse_result::invalid;
   }
   return std::isfinite(value) ? floating_parse_result::success : floating_parse_result::non_finite;
}

std::string floating_diagnostic(floating_parse_result result) {
   switch (result) {
   case floating_parse_result::success:
      return {};
   case floating_parse_result::invalid:
      return "expected a number with no trailing characters";
   case floating_parse_result::out_of_range:
      return "number is out of range";
   case floating_parse_result::non_finite:
      return "expected a finite number";
   }
   return "expected a number";
}

std::string validate_scalar_text(value_kind type, std::string_view input) {
   if (type == value_kind::text) {
      return {};
   }
   if (type == value_kind::integer) {
      auto value = std::int64_t{};
      const auto [end, error] = std::from_chars(input.data(), input.data() + input.size(), value);
      if (error == std::errc{} && end == input.data() + input.size()) {
         return {};
      }
      return "expected an integer";
   }
   if (type == value_kind::real) {
      auto value = double{};
      return floating_diagnostic(parse_floating(input, value));
   }
   if (parse_boolean(input)) {
      return {};
   }
   return "expected true, false, 1, 0, yes, no, on, or off";
}

scalar_value convert_scalar(value_kind type, std::string_view input) {
   if (type == value_kind::text) {
      return std::string{input};
   }
   if (type == value_kind::integer) {
      auto value = std::int64_t{};
      const auto [end, error] = std::from_chars(input.data(), input.data() + input.size(), value);
      if (error == std::errc{} && end == input.data() + input.size()) {
         return value;
      }
   } else if (type == value_kind::real) {
      auto value = double{};
      if (parse_floating(input, value) == floating_parse_result::success) {
         return value;
      }
   } else if (const auto value = parse_boolean(input)) {
      return *value;
   }

   FORGE_THROW_EXCEPTION(exceptions::parse_failed, "typed CLI value conversion failed",
                         forge::exceptions::ctx("expected", type_name(type)));
}

std::optional<std::string> validate_option_shape(const option& value) {
   if (!is_kebab_name(value.name)) {
      return "option name must use lower-case kebab-case: " + value.name;
   }
   auto spellings = std::unordered_set<std::string>{"--" + value.name};
   const auto reserved = std::unordered_set<std::string>{
       "-h",
       "--help",
       "--help-all",
       "--version",
   };
   if (reserved.contains("--" + value.name)) {
      return "option spelling is reserved by Forge CLI: --" + value.name;
   }
   for (const auto& alias : value.aliases) {
      if (!is_option_alias(alias)) {
         return "option alias is not canonical: " + alias;
      }
      if (!spellings.insert(alias).second) {
         return "option spelling is duplicated: " + alias;
      }
      if (reserved.contains(alias)) {
         return "option spelling is reserved by Forge CLI: " + alias;
      }
   }
   if (value.form == option_form::flag && !value.value_name.empty()) {
      return "flag option must not define a value name: " + value.name;
   }
   return std::nullopt;
}

std::optional<std::string> validate_argument_shape(const argument& value, bool last) {
   if (!is_kebab_name(value.name)) {
      return "argument name must use lower-case kebab-case: " + value.name;
   }
   if (value.max_count != 0 && value.min_count > value.max_count) {
      return "argument minimum exceeds maximum: " + value.name;
   }
   if (value.min_count > static_cast<std::size_t>((std::numeric_limits<int>::max)()) ||
       value.max_count > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
      return "argument arity exceeds parser limits: " + value.name;
   }
   if (!last && (value.min_count != 1 || value.max_count != 1)) {
      return "only the final positional argument may have variable arity: " + value.name;
   }
   return std::nullopt;
}

std::optional<std::string> validate_option_set(std::span<const option* const> ancestors,
                                               const std::vector<option>& local, std::string_view location) {
   auto keys = std::unordered_set<std::string>{};
   auto spellings = std::unordered_set<std::string>{};
   auto visible = std::unordered_set<std::string>{};

   const auto add = [&](const option& value) -> std::optional<std::string> {
      if (const auto error = validate_option_shape(value)) {
         return error;
      }
      if (!keys.insert(value.name).second) {
         return "option name is ambiguous at " + std::string{location} + ": " + value.name;
      }
      visible.insert(value.name);
      for (const auto& spelling : option_spellings(value)) {
         if (!spellings.insert(spelling).second) {
            return "option spelling is ambiguous at " + std::string{location} + ": " + spelling;
         }
      }
      return std::nullopt;
   };

   for (const auto* value : ancestors) {
      if (const auto error = add(*value)) {
         return error;
      }
   }
   for (const auto& value : local) {
      if (const auto error = add(value)) {
         return error;
      }
   }

   for (const auto& value : local) {
      for (const auto& conflict : value.conflicts_with) {
         if (!visible.contains(conflict)) {
            return "option conflict references an unknown option at " + std::string{location} + ": " + conflict;
         }
      }
      for (const auto& requirement : value.requires_options) {
         if (!visible.contains(requirement)) {
            return "option requirement references an unknown option at " + std::string{location} + ": " + requirement;
         }
      }
   }
   return std::nullopt;
}

std::optional<std::string> validate_commands(const std::vector<command>& commands,
                                             const std::vector<const option*>& ancestors,
                                             const std::vector<std::string>& parent_path) {
   auto sibling_names = std::unordered_set<std::string>{};
   for (const auto& value : commands) {
      if (!is_kebab_name(value.name)) {
         return "command name must use lower-case kebab-case: " + value.name;
      }
      if (!sibling_names.insert(value.name).second) {
         return "duplicate command name: " + value.name;
      }
      for (const auto& alias : value.aliases) {
         if (!is_kebab_name(alias)) {
            return "command alias must use lower-case kebab-case: " + alias;
         }
         if (!sibling_names.insert(alias).second) {
            return "duplicate command spelling: " + alias;
         }
      }
   }

   for (const auto& value : commands) {
      auto path = parent_path;
      path.push_back(value.name);
      const auto location = command_location(path);
      if (value.require_subcommand && value.commands.empty()) {
         return "command requires a subcommand but has none: " + location;
      }
      if (!value.handler && value.commands.empty()) {
         return "command has no handler: " + location;
      }
      if (const auto error = validate_option_set(ancestors, value.options, location)) {
         return error;
      }
      for (auto index = std::size_t{0}; index < value.arguments.size(); ++index) {
         if (const auto error = validate_argument_shape(value.arguments[index], index + 1 == value.arguments.size())) {
            return error;
         }
      }

      auto active = ancestors;
      for (const auto& option : value.options) {
         active.push_back(&option);
      }
      if (const auto error = validate_commands(value.commands, active, path)) {
         return error;
      }
   }
   return std::nullopt;
}

std::optional<std::string> descriptor_error(const application& app) {
   if (!is_application_name(app.name)) {
      return "application name must use lower-case words separated by dashes or underscores";
   }
   if (app.require_command && app.commands.empty()) {
      return "application requires a command but defines none";
   }
   if (!app.handler && app.commands.empty()) {
      return "application has no command or default handler";
   }
   if (const auto error = validate_option_set(std::span<const option* const>{}, app.options, "<root>")) {
      return error;
   }
   auto globals = std::vector<const option*>{};
   globals.reserve(app.options.size());
   for (const auto& option : app.options) {
      globals.push_back(&option);
   }
   return validate_commands(app.commands, globals, {});
}

CLI::Validator scalar_validator(value_kind type, std::string description) {
   if (description.empty()) {
      description = type_name(type);
   }
   return CLI::Validator{
       [type](std::string& input) { return validate_scalar_text(type, input); },
       std::move(description),
       "FORGE_TYPE",
   };
}

void configure_option(CLI::App& app, const std::shared_ptr<option_state>& state, bool inherited) {
   const auto& specification = *state->specification;
   auto description = specification.description;
   if (specification.required) {
      description += (description.empty() ? "Required" : " (required)");
   }
   CLI::Option* backend = nullptr;
   if (specification.form == option_form::flag) {
      backend = app.add_flag_function(
          join_spellings(specification),
          [state](std::int64_t count) {
             if (count > 0) {
                state->occurrences += static_cast<std::size_t>(count);
             }
          },
          description);
      backend->disable_flag_override();
   } else {
      backend = app.add_option(
          join_spellings(specification),
          [state](const CLI::results_t& values) {
             state->occurrences += values.size();
             state->raw_values.insert(state->raw_values.end(), values.begin(), values.end());
             return true;
          },
          description);
      backend->type_size(1)->expected(1)->check(scalar_validator(specification.type, specification.validation_hint));
      backend->type_name(specification.value_name.empty() ? type_name(specification.type) : specification.value_name);
      backend->multi_option_policy(specification.repeatable ? CLI::MultiOptionPolicy::TakeAll
                                                            : CLI::MultiOptionPolicy::Throw);
   }
   if (inherited) {
      backend->group("Inherited options");
   }
}

void configure_argument(CLI::App& app, const std::shared_ptr<argument_state>& state) {
   const auto& specification = *state->specification;
   auto* backend = app.add_option(
       specification.name,
       [state](const CLI::results_t& values) {
          state->raw_values.insert(state->raw_values.end(), values.begin(), values.end());
          return true;
       },
       specification.description);
   backend->type_size(1)
       ->expected(static_cast<int>(specification.min_count),
                  specification.max_count == 0 ? -1 : static_cast<int>(specification.max_count))
       ->check(scalar_validator(specification.type, specification.validation_hint))
       ->type_name(type_name(specification.type));
   if (specification.min_count > 0) {
      backend->required();
   }
   if (specification.max_count == 0 || specification.max_count > 1) {
      backend->multi_option_policy(CLI::MultiOptionPolicy::TakeAll);
   }
}

class parser_backend {
 public:
   explicit parser_backend(const application& specification)
       : specification_(specification),
         backend_(specification.description.empty() ? specification.summary : specification.description,
                  specification.name) {
      if (const auto error = descriptor_error(specification_)) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_descriptor, *error);
      }

      backend_.set_help_flag("-h,--help", "Show help and exit");
      backend_.set_help_all_flag("--help-all", "Show expanded help and exit");
      if (!specification_.version.empty()) {
         backend_.set_version_flag("--version", version_text(), "Show version and exit");
      }
      backend_.require_subcommand(
          specification_.require_command || (!specification_.handler && !specification_.commands.empty()) ? 1 : 0, 1);
      backend_.subcommand_fallthrough(false);

      root_.backend = &backend_;
      for (const auto& option : specification_.options) {
         auto state = std::make_shared<option_state>();
         state->specification = &option;
         root_.local_options.push_back(state);
         configure_option(backend_, state, false);
      }

      auto inherited = root_.local_options;
      for (const auto& command : specification_.commands) {
         add_command(root_, command, inherited);
      }
   }

   parse_outcome parse_arguments(std::span<const std::string_view> arguments) {
      auto backend_arguments = std::vector<std::string>{};
      backend_arguments.reserve(arguments.size());
      for (auto item = arguments.rbegin(); item != arguments.rend(); ++item) {
         backend_arguments.emplace_back(*item);
      }

      try {
         backend_.parse(std::move(backend_arguments));
      } catch (const CLI::CallForAllHelp&) {
         const auto& selected = deepest_selected();
         return help_outcome{
             .command_path = selected.path,
             .text = help_text(selected, true),
             .expanded = true,
         };
      } catch (const CLI::CallForHelp&) {
         const auto& selected = deepest_selected();
         return help_outcome{
             .command_path = selected.path,
             .text = help_text(selected, false),
             .expanded = false,
         };
      } catch (const CLI::CallForVersion&) {
         return version_outcome{.text = version_text() + '\n'};
      } catch (const CLI::ParseError& error) {
         FORGE_THROW_EXCEPTION(exceptions::parse_failed, error.what(), forge::exceptions::ctx("parser", "CLI11"),
                               forge::exceptions::ctx("parse_code", error.get_exit_code()));
      } catch (const CLI::Error& error) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_descriptor, error.what(), forge::exceptions::ctx("parser", "CLI11"));
      }

      const auto& selected = deepest_selected();
      auto input = make_invocation(selected);
      validate_invocation(selected, input);
      auto handler = selected.specification == nullptr ? specification_.handler : selected.specification->handler;
      if (!handler) {
         FORGE_THROW_EXCEPTION(exceptions::dispatch_failed, "selected command has no handler",
                               forge::exceptions::ctx("command", command_location(selected.path)));
      }
      return dispatch_outcome{.input = std::move(input), .handler = std::move(handler)};
   }

   std::string help_for(std::span<const std::string_view> path, bool expanded) const {
      const auto* selected = &root_;
      for (const auto part : path) {
         const auto found =
             std::find_if(selected->children.begin(), selected->children.end(), [part](const auto& child) {
                if (child->specification->name == part) {
                   return true;
                }
                return std::find(child->specification->aliases.begin(), child->specification->aliases.end(), part) !=
                       child->specification->aliases.end();
             });
         if (found == selected->children.end()) {
            FORGE_THROW_EXCEPTION(exceptions::parse_failed, "unknown command in help path",
                                  forge::exceptions::ctx("command", std::string{part}));
         }
         selected = found->get();
      }
      return help_text(*selected, expanded);
   }

 private:
   void add_command(backend_node& parent, const command& specification,
                    const std::vector<std::shared_ptr<option_state>>& inherited) {
      auto node = std::make_unique<backend_node>();
      node->parent = &parent;
      node->specification = &specification;
      node->path = parent.path;
      node->path.push_back(specification.name);
      node->backend = parent.backend->add_subcommand(
          specification.name, specification.description.empty() ? specification.summary : specification.description);
      for (const auto& alias : specification.aliases) {
         node->backend->alias(alias);
      }
      node->backend->require_subcommand(
          specification.require_subcommand || (!specification.handler && !specification.commands.empty()) ? 1 : 0, 1);
      node->backend->subcommand_fallthrough(false);
      if (!specification_.version.empty()) {
         node->backend->set_version_flag("--version", version_text(), "Show version and exit");
      }

      for (const auto& option : inherited) {
         configure_option(*node->backend, option, true);
      }

      auto child_inherited = inherited;
      for (const auto& option : specification.options) {
         auto state = std::make_shared<option_state>();
         state->specification = &option;
         node->local_options.push_back(state);
         configure_option(*node->backend, state, false);
         if (option.inherited) {
            child_inherited.push_back(state);
         }
      }
      for (const auto& argument : specification.arguments) {
         auto state = std::make_shared<argument_state>();
         state->specification = &argument;
         node->arguments.push_back(state);
         configure_argument(*node->backend, state);
      }

      auto* added = node.get();
      parent.children.push_back(std::move(node));
      for (const auto& command : specification.commands) {
         add_command(*added, command, child_inherited);
      }
   }

   const backend_node& deepest_selected() const {
      const auto* selected = &root_;
      while (true) {
         const auto found = std::find_if(selected->children.begin(), selected->children.end(),
                                         [](const auto& child) { return child->backend->parsed(); });
         if (found == selected->children.end()) {
            return *selected;
         }
         selected = found->get();
      }
   }

   std::vector<const backend_node*> active_nodes(const backend_node& selected) const {
      auto result = std::vector<const backend_node*>{};
      for (auto* current = &selected; current != nullptr; current = current->parent) {
         result.push_back(current);
      }
      std::reverse(result.begin(), result.end());
      return result;
   }

   parsed_value parse_option(const option_state& state) const {
      auto result = parsed_value{
          .name = state.specification->name,
          .occurrences = state.occurrences,
      };
      if (state.specification->form == option_form::flag) {
         result.values.reserve(state.occurrences);
         for (auto index = std::size_t{0}; index < state.occurrences; ++index) {
            result.values.emplace_back(true);
         }
      } else {
         result.values.reserve(state.raw_values.size());
         for (const auto& value : state.raw_values) {
            result.values.push_back(convert_scalar(state.specification->type, value));
         }
      }
      return result;
   }

   parsed_value parse_argument(const argument_state& state) const {
      auto result = parsed_value{
          .name = state.specification->name,
          .occurrences = state.raw_values.size(),
      };
      result.values.reserve(state.raw_values.size());
      for (const auto& value : state.raw_values) {
         result.values.push_back(convert_scalar(state.specification->type, value));
      }
      return result;
   }

   invocation make_invocation(const backend_node& selected) const {
      auto result = invocation{.command_path = selected.path};
      for (const auto* node : active_nodes(selected)) {
         for (const auto& state : node->local_options) {
            if (state->occurrences > 0) {
               result.options.push_back(parse_option(*state));
            }
         }
         for (const auto& state : node->arguments) {
            if (!state->raw_values.empty()) {
               result.arguments.push_back(parse_argument(*state));
            }
         }
      }
      return result;
   }

   void validate_value(const parsed_value& value, const value_validator& validator, std::string_view category) const {
      if (!validator) {
         return;
      }
      try {
         if (const auto error = validator(value)) {
            FORGE_THROW_EXCEPTION(exceptions::validation_failed, *error, forge::exceptions::ctx("kind", category),
                                  forge::exceptions::ctx("name", value.name));
         }
      } catch (const forge::exceptions::base&) {
         throw;
      } catch (const std::exception& error) {
         FORGE_THROW_EXCEPTION(exceptions::validation_failed, "CLI value validator failed",
                               forge::exceptions::ctx("kind", category), forge::exceptions::ctx("name", value.name),
                               forge::exceptions::ctx("reason", error.what()));
      } catch (...) {
         FORGE_THROW_EXCEPTION(exceptions::validation_failed, "CLI value validator failed",
                               forge::exceptions::ctx("kind", category), forge::exceptions::ctx("name", value.name));
      }
   }

   void validate_command_callback(const command_validator& validator, const invocation& input,
                                  std::string_view location) const {
      try {
         if (const auto error = validator(input)) {
            FORGE_THROW_EXCEPTION(exceptions::validation_failed, *error, forge::exceptions::ctx("command", location));
         }
      } catch (const forge::exceptions::base&) {
         throw;
      } catch (const std::exception& error) {
         FORGE_THROW_EXCEPTION(exceptions::validation_failed, "CLI command validator failed",
                               forge::exceptions::ctx("command", location),
                               forge::exceptions::ctx("reason", error.what()));
      } catch (...) {
         FORGE_THROW_EXCEPTION(exceptions::validation_failed, "CLI command validator failed",
                               forge::exceptions::ctx("command", location));
      }
   }

   void validate_invocation(const backend_node& selected, const invocation& input) const {
      for (const auto* node : active_nodes(selected)) {
         for (const auto& state : node->local_options) {
            const auto* parsed = input.find_option(state->specification->name);
            if (state->specification->required && parsed == nullptr) {
               FORGE_THROW_EXCEPTION(exceptions::validation_failed, "required CLI option is missing",
                                     forge::exceptions::ctx("option", state->specification->name));
            }
            if (state->occurrences > 1 && !state->specification->repeatable) {
               FORGE_THROW_EXCEPTION(exceptions::validation_failed, "CLI option is not repeatable",
                                     forge::exceptions::ctx("option", state->specification->name));
            }
            if (parsed == nullptr) {
               continue;
            }
            for (const auto& conflict : state->specification->conflicts_with) {
               if (input.has_option(conflict)) {
                  FORGE_THROW_EXCEPTION(exceptions::validation_failed, "conflicting CLI options were provided",
                                        forge::exceptions::ctx("option", state->specification->name),
                                        forge::exceptions::ctx("conflict", conflict));
               }
            }
            for (const auto& requirement : state->specification->requires_options) {
               if (!input.has_option(requirement)) {
                  FORGE_THROW_EXCEPTION(exceptions::validation_failed, "required companion CLI option is missing",
                                        forge::exceptions::ctx("option", state->specification->name),
                                        forge::exceptions::ctx("requires", requirement));
               }
            }
            validate_value(*parsed, state->specification->validate, "option");
         }
         for (const auto& state : node->arguments) {
            if (const auto* parsed = input.find_argument(state->specification->name)) {
               validate_value(*parsed, state->specification->validate, "argument");
            }
         }
      }

      for (const auto& validator : specification_.validators) {
         validate_command_callback(validator, input, "<root>");
      }
      for (const auto* node : active_nodes(selected)) {
         if (node->specification == nullptr) {
            continue;
         }
         const auto location = command_location(node->path);
         for (const auto& validator : node->specification->validators) {
            validate_command_callback(validator, input, location);
         }
      }
   }

   std::string previous_path(const backend_node& selected) const {
      if (selected.path.empty()) {
         return {};
      }
      auto result = specification_.name;
      for (auto index = std::size_t{0}; index + 1 < selected.path.size(); ++index) {
         result += ' ';
         result += selected.path[index];
      }
      return result;
   }

   std::string help_text(const backend_node& selected, bool expanded) const {
      return selected.backend->help(previous_path(selected),
                                    expanded ? CLI::AppFormatMode::All : CLI::AppFormatMode::Normal);
   }

   std::string version_text() const {
      return specification_.version.empty() ? specification_.name : specification_.name + " " + specification_.version;
   }

   const application& specification_;
   CLI::App backend_;
   backend_node root_;
};

} // namespace

parse_outcome parse(const application& app, std::span<const std::string_view> arguments) {
   try {
      auto backend = parser_backend{app};
      return backend.parse_arguments(arguments);
   } catch (const CLI::Error& error) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_descriptor, error.what(), forge::exceptions::ctx("parser", "CLI11"));
   }
}

std::string render_help(const application& app, std::span<const std::string_view> command_path, bool expanded) {
   try {
      const auto backend = parser_backend{app};
      return backend.help_for(command_path, expanded);
   } catch (const CLI::Error& error) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_descriptor, error.what(), forge::exceptions::ctx("parser", "CLI11"));
   }
}

} // namespace forge::cli
