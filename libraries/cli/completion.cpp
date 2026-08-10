module;

#include <algorithm>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

module forge.cli.completion;

import forge.cli.command;

namespace forge::cli {
namespace {

struct completion_context {
   const command_descriptor* command = nullptr;
   std::vector<const option_descriptor*> inherited;
   std::unordered_set<std::string> used_options;
   std::size_t positional_values = 0;
};

std::string canonical_spelling(const option_descriptor& option) {
   return "--" + option.name;
}

bool matches_command(const command_descriptor& command, std::string_view word) {
   if (command.name == word) {
      return true;
   }
   return std::find(command.aliases.begin(), command.aliases.end(), word) != command.aliases.end();
}

bool matches_option(const option_descriptor& option, std::string_view word) {
   if (canonical_spelling(option) == word) {
      return true;
   }
   return std::find(option.aliases.begin(), option.aliases.end(), word) != option.aliases.end();
}

std::vector<const option_descriptor*> visible_options(const application_descriptor& descriptor,
                                                      const completion_context& context) {
   auto result = std::vector<const option_descriptor*>{};
   result.reserve(descriptor.options.size() + context.inherited.size() +
                  (context.command == nullptr ? 0 : context.command->options.size()));
   for (const auto& option : descriptor.options) {
      result.push_back(&option);
   }
   result.insert(result.end(), context.inherited.begin(), context.inherited.end());
   if (context.command != nullptr) {
      for (const auto& option : context.command->options) {
         result.push_back(&option);
      }
   }
   return result;
}

const std::vector<command_descriptor>& child_commands(const application_descriptor& descriptor,
                                                      const completion_context& context) {
   return context.command == nullptr ? descriptor.commands : context.command->commands;
}

const option_descriptor* find_option(const application_descriptor& descriptor, const completion_context& context,
                                     std::string_view word) {
   for (const auto* option : visible_options(descriptor, context)) {
      if (matches_option(*option, word)) {
         return option;
      }
   }
   return nullptr;
}

void enter_command(completion_context& context, const command_descriptor& command) {
   if (context.command != nullptr) {
      for (const auto& option : context.command->options) {
         if (option.inherited) {
            context.inherited.push_back(&option);
         }
      }
   }
   context.command = &command;
   context.positional_values = 0;
}

const argument_descriptor* active_argument(const completion_context& context) {
   if (context.command == nullptr) {
      return nullptr;
   }
   auto consumed = context.positional_values;
   for (const auto& argument : context.command->arguments) {
      if (argument.max_count == 0 || consumed < argument.max_count) {
         return &argument;
      }
      consumed -= argument.max_count;
   }
   return nullptr;
}

completion_context consume_words(const application_descriptor& descriptor, std::span<const std::string_view> words) {
   auto context = completion_context{};
   for (auto index = std::size_t{0}; index < words.size(); ++index) {
      const auto word = words[index];
      if (const auto equals = word.find('='); equals != std::string_view::npos && word.starts_with("--")) {
         if (const auto* option = find_option(descriptor, context, word.substr(0, equals))) {
            context.used_options.insert(option->name);
         }
         continue;
      }
      if (const auto* option = find_option(descriptor, context, word)) {
         context.used_options.insert(option->name);
         if (option->form == option_form::value && index + 1 < words.size()) {
            ++index;
         }
         continue;
      }
      const auto& commands = child_commands(descriptor, context);
      const auto found = std::find_if(commands.begin(), commands.end(),
                                      [word](const auto& command) { return matches_command(command, word); });
      if (found != commands.end()) {
         enter_command(context, *found);
         continue;
      }
      ++context.positional_values;
   }
   return context;
}

void append_if_matching(std::vector<completion_item>& output, std::unordered_set<std::string>& seen, std::string text,
                        std::string description, completion_item_kind kind, std::string_view prefix) {
   if (!std::string_view{text}.starts_with(prefix) || !seen.insert(text).second) {
      return;
   }
   output.push_back({
       .text = std::move(text),
       .description = std::move(description),
       .kind = kind,
   });
}

std::vector<completion_item> value_completions(const option_descriptor& option, std::string_view prefix,
                                               std::string_view assignment_prefix = {}) {
   auto result = std::vector<completion_item>{};
   auto seen = std::unordered_set<std::string>{};
   for (const auto& value : option.completion_values) {
      if (!std::string_view{value}.starts_with(prefix)) {
         continue;
      }
      append_if_matching(result, seen, std::string{assignment_prefix} + value, option.description,
                         completion_item_kind::value, assignment_prefix);
   }
   return result;
}

} // namespace

std::vector<completion_item> complete(const application_descriptor& descriptor,
                                      std::span<const std::string_view> words) {
   const auto prefix = words.empty() ? std::string_view{} : words.back();
   const auto committed = words.empty() ? words : words.first(words.size() - 1);
   auto context = consume_words(descriptor, committed);

   if (!committed.empty()) {
      if (const auto* option = find_option(descriptor, context, committed.back());
          option != nullptr && option->form == option_form::value) {
         return value_completions(*option, prefix);
      }
   }

   if (prefix.starts_with("--")) {
      if (const auto equals = prefix.find('='); equals != std::string_view::npos) {
         if (const auto* option = find_option(descriptor, context, prefix.substr(0, equals));
             option != nullptr && option->form == option_form::value) {
            const auto assignment = prefix.substr(0, equals + 1);
            return value_completions(*option, prefix.substr(equals + 1), assignment);
         }
      }
   }

   auto result = std::vector<completion_item>{};
   auto seen = std::unordered_set<std::string>{};
   if (!prefix.starts_with('-')) {
      for (const auto& command : child_commands(descriptor, context)) {
         const auto description = command.summary.empty() ? command.description : command.summary;
         append_if_matching(result, seen, command.name, description, completion_item_kind::command, prefix);
         for (const auto& alias : command.aliases) {
            append_if_matching(result, seen, alias, description, completion_item_kind::command, prefix);
         }
      }
      if (const auto* argument = active_argument(context)) {
         for (const auto& value : argument->completion_values) {
            append_if_matching(result, seen, value, argument->description, completion_item_kind::value, prefix);
         }
      }
   }

   if (prefix.empty() || prefix.starts_with('-')) {
      for (const auto* option : visible_options(descriptor, context)) {
         if (!option->repeatable && context.used_options.contains(option->name)) {
            continue;
         }
         append_if_matching(result, seen, canonical_spelling(*option), option->description,
                            completion_item_kind::option, prefix);
         for (const auto& alias : option->aliases) {
            append_if_matching(result, seen, alias, option->description, completion_item_kind::option, prefix);
         }
      }
      append_if_matching(result, seen, "--help", "Show help and exit", completion_item_kind::option, prefix);
      append_if_matching(result, seen, "--help-all", "Show expanded help and exit", completion_item_kind::option,
                         prefix);
      if (!descriptor.version.empty()) {
         append_if_matching(result, seen, "--version", "Show version and exit", completion_item_kind::option, prefix);
      }
   }

   std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) { return left.text < right.text; });
   return result;
}

} // namespace forge::cli
