module;

#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

export module forge.cli.parser;

export import forge.cli.command;
export import forge.cli.exceptions;

export namespace forge::cli {

struct dispatch_outcome {
   invocation input;
   command_handler handler;
};

struct help_outcome {
   std::vector<std::string> command_path;
   std::string text;
   bool expanded = false;
};

struct version_outcome {
   std::string text;
};

using parse_outcome = std::variant<dispatch_outcome, help_outcome, version_outcome>;

[[nodiscard]] parse_outcome parse(const application& app, std::span<const std::string_view> arguments);

[[nodiscard]] std::string render_help(const application& app, std::span<const std::string_view> command_path = {},
                                      bool expanded = false);

} // namespace forge::cli
