module;

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

module forge.cli.command;

namespace forge::cli {
namespace {

option_descriptor describe_option(const option& value) {
   return {
       .name = value.name,
       .aliases = value.aliases,
       .description = value.description,
       .value_name = value.value_name,
       .form = value.form,
       .type = value.type,
       .required = value.required,
       .repeatable = value.repeatable,
       .inherited = value.inherited,
       .conflicts_with = value.conflicts_with,
       .requires_options = value.requires_options,
       .completion_values = value.completion_values,
       .validation_hint = value.validation_hint,
   };
}

argument_descriptor describe_argument(const argument& value) {
   return {
       .name = value.name,
       .description = value.description,
       .type = value.type,
       .min_count = static_cast<std::uint64_t>(value.min_count),
       .max_count = static_cast<std::uint64_t>(value.max_count),
       .completion_values = value.completion_values,
       .validation_hint = value.validation_hint,
   };
}

command_descriptor describe_command(const command& value) {
   auto result = command_descriptor{
       .name = value.name,
       .aliases = value.aliases,
       .summary = value.summary,
       .description = value.description,
       .require_subcommand = value.require_subcommand,
       .executable = static_cast<bool>(value.handler),
   };
   result.options.reserve(value.options.size());
   for (const auto& option : value.options) {
      result.options.push_back(describe_option(option));
   }
   result.arguments.reserve(value.arguments.size());
   for (const auto& argument : value.arguments) {
      result.arguments.push_back(describe_argument(argument));
   }
   result.commands.reserve(value.commands.size());
   for (const auto& command : value.commands) {
      result.commands.push_back(describe_command(command));
   }
   return result;
}

const parsed_value* find_value(const std::vector<parsed_value>& values, std::string_view name) noexcept {
   const auto found =
       std::find_if(values.begin(), values.end(), [name](const auto& value) { return value.name == name; });
   return found == values.end() ? nullptr : &*found;
}

} // namespace

const parsed_value* invocation::find_option(std::string_view name) const noexcept {
   return find_value(options, name);
}

const parsed_value* invocation::find_argument(std::string_view name) const noexcept {
   return find_value(arguments, name);
}

bool invocation::has_option(std::string_view name) const noexcept {
   return find_option(name) != nullptr;
}

application_descriptor describe(const application& value) {
   auto result = application_descriptor{
       .name = value.name,
       .version = value.version,
       .summary = value.summary,
       .description = value.description,
       .require_command = value.require_command,
       .executable = static_cast<bool>(value.handler),
   };
   result.options.reserve(value.options.size());
   for (const auto& option : value.options) {
      auto descriptor = describe_option(option);
      descriptor.inherited = true;
      result.options.push_back(std::move(descriptor));
   }
   result.commands.reserve(value.commands.size());
   for (const auto& command : value.commands) {
      result.commands.push_back(describe_command(command));
   }
   return result;
}

} // namespace forge::cli
