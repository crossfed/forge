module;

#include <boost/asio/awaitable.hpp>
#include <boost/describe.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

export module forge.cli.command;

export namespace forge::cli {

enum class value_kind : std::uint8_t {
   text,
   integer,
   real,
   boolean,
};

enum class option_form : std::uint8_t {
   flag,
   value,
};

using scalar_value = std::variant<std::string, std::int64_t, double, bool>;

struct parsed_value {
   std::string name;
   std::vector<scalar_value> values;
   std::size_t occurrences = 0;
};

struct invocation {
   std::vector<std::string> command_path;
   std::vector<parsed_value> options;
   std::vector<parsed_value> arguments;

   [[nodiscard]] const parsed_value* find_option(std::string_view name) const noexcept;
   [[nodiscard]] const parsed_value* find_argument(std::string_view name) const noexcept;
   [[nodiscard]] bool has_option(std::string_view name) const noexcept;
};

using value_validator = std::function<std::optional<std::string>(const parsed_value&)>;
using command_validator = std::function<std::optional<std::string>(const invocation&)>;
using command_handler = std::function<boost::asio::awaitable<int>(const invocation&, std::stop_token)>;

struct option {
   std::string name;
   std::vector<std::string> aliases;
   std::string description;
   std::string value_name;
   option_form form = option_form::value;
   value_kind type = value_kind::text;
   bool required = false;
   bool repeatable = false;
   bool inherited = false;
   std::vector<std::string> conflicts_with;
   std::vector<std::string> requires_options;
   std::vector<std::string> completion_values;
   std::string validation_hint;
   value_validator validate;
};

struct argument {
   std::string name;
   std::string description;
   value_kind type = value_kind::text;
   std::size_t min_count = 1;
   std::size_t max_count = 1;
   std::vector<std::string> completion_values;
   std::string validation_hint;
   value_validator validate;
};

struct command {
   std::string name;
   std::vector<std::string> aliases;
   std::string summary;
   std::string description;
   bool require_subcommand = false;
   std::vector<option> options;
   std::vector<argument> arguments;
   std::vector<command> commands;
   std::vector<command_validator> validators;
   command_handler handler;
};

struct application {
   std::string name;
   std::string version;
   std::string summary;
   std::string description;
   bool require_command = true;
   std::vector<option> options;
   std::vector<command> commands;
   std::vector<command_validator> validators;
   command_handler handler;
};

struct option_descriptor {
   std::string name;
   std::vector<std::string> aliases;
   std::string description;
   std::string value_name;
   option_form form = option_form::value;
   value_kind type = value_kind::text;
   bool required = false;
   bool repeatable = false;
   bool inherited = false;
   std::vector<std::string> conflicts_with;
   std::vector<std::string> requires_options;
   std::vector<std::string> completion_values;
   std::string validation_hint;

   bool operator==(const option_descriptor&) const = default;
};

struct argument_descriptor {
   std::string name;
   std::string description;
   value_kind type = value_kind::text;
   std::uint64_t min_count = 1;
   std::uint64_t max_count = 1;
   std::vector<std::string> completion_values;
   std::string validation_hint;

   bool operator==(const argument_descriptor&) const = default;
};

struct command_descriptor {
   std::string name;
   std::vector<std::string> aliases;
   std::string summary;
   std::string description;
   bool require_subcommand = false;
   bool executable = false;
   std::vector<option_descriptor> options;
   std::vector<argument_descriptor> arguments;
   std::vector<command_descriptor> commands;

   bool operator==(const command_descriptor&) const = default;
};

struct application_descriptor {
   std::string name;
   std::string version;
   std::string summary;
   std::string description;
   bool require_command = true;
   bool executable = false;
   std::vector<option_descriptor> options;
   std::vector<command_descriptor> commands;

   bool operator==(const application_descriptor&) const = default;
};

BOOST_DESCRIBE_ENUM(value_kind, text, integer, real, boolean)
BOOST_DESCRIBE_ENUM(option_form, flag, value)
BOOST_DESCRIBE_STRUCT(option_descriptor, (),
                      (name, aliases, description, value_name, form, type, required, repeatable, inherited,
                       conflicts_with, requires_options, completion_values, validation_hint))
BOOST_DESCRIBE_STRUCT(argument_descriptor, (),
                      (name, description, type, min_count, max_count, completion_values, validation_hint))
BOOST_DESCRIBE_STRUCT(command_descriptor, (),
                      (name, aliases, summary, description, require_subcommand, executable, options, arguments,
                       commands))
BOOST_DESCRIBE_STRUCT(application_descriptor, (),
                      (name, version, summary, description, require_command, executable, options, commands))

[[nodiscard]] application_descriptor describe(const application& value);

} // namespace forge::cli
