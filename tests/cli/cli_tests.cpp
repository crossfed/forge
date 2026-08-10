#include <boost/asio/awaitable.hpp>
#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <stop_token>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

import forge.cli.command;
import forge.cli.completion;
import forge.cli.exceptions;
import forge.cli.parser;
import forge.cli.runner;
import forge.cli.terminal;
import forge.codec.json;

namespace {

struct dispatch_capture {
   bool called = false;
   bool stop_requested = false;
   forge::cli::invocation input;
};

forge::cli::application make_application(const std::shared_ptr<dispatch_capture>& capture) {
   auto endpoint = forge::cli::option{};
   endpoint.name = "endpoint";
   endpoint.aliases = {"-e"};
   endpoint.description = "Remote endpoint";
   endpoint.value_name = "URL";
   endpoint.required = true;

   auto verbose = forge::cli::option{};
   verbose.name = "verbose";
   verbose.aliases = {"-v"};
   verbose.description = "Increase verbosity";
   verbose.form = forge::cli::option_form::flag;
   verbose.repeatable = true;

   auto profile = forge::cli::option{};
   profile.name = "profile";
   profile.description = "Account profile";
   profile.inherited = true;

   auto local = forge::cli::option{};
   local.name = "local";
   local.description = "Account-local value";

   auto format = forge::cli::option{};
   format.name = "format";
   format.aliases = {"-f"};
   format.description = "Output format";
   format.completion_values = {"json", "text"};
   format.conflicts_with = {"raw"};
   format.validate = [](const forge::cli::parsed_value& value) -> std::optional<std::string> {
      const auto& text = std::get<std::string>(value.values.front());
      if (text != "json" && text != "text") {
         return "format must be json or text";
      }
      return std::nullopt;
   };

   auto raw = forge::cli::option{};
   raw.name = "raw";
   raw.description = "Emit raw output";
   raw.form = forge::cli::option_form::flag;

   auto tag = forge::cli::option{};
   tag.name = "tag";
   tag.aliases = {"-t"};
   tag.description = "Attach a tag";
   tag.repeatable = true;

   auto ratio = forge::cli::option{};
   ratio.name = "ratio";
   ratio.description = "Floating-point ratio";
   ratio.type = forge::cli::value_kind::real;

   auto account_id = forge::cli::argument{};
   account_id.name = "account-id";
   account_id.description = "Numeric account identifier";
   account_id.type = forge::cli::value_kind::integer;
   account_id.completion_values = {"42", "77"};
   account_id.validate = [](const forge::cli::parsed_value& value) -> std::optional<std::string> {
      if (std::get<std::int64_t>(value.values.front()) <= 0) {
         return "account-id must be positive";
      }
      return std::nullopt;
   };

   auto show = forge::cli::command{};
   show.name = "show";
   show.aliases = {"ls"};
   show.summary = "Show an account";
   show.options = {std::move(format), std::move(raw), std::move(tag), std::move(ratio)};
   show.arguments = {std::move(account_id)};
   show.validators.push_back([](const forge::cli::invocation& input) -> std::optional<std::string> {
      const auto* endpoint = input.find_option("endpoint");
      if (endpoint != nullptr && std::get<std::string>(endpoint->values.front()) == "blocked") {
         return "endpoint is blocked by command validation";
      }
      return std::nullopt;
   });
   show.handler = [capture](const forge::cli::invocation& input, std::stop_token stop) -> boost::asio::awaitable<int> {
      capture->called = true;
      capture->stop_requested = stop.stop_requested();
      capture->input = input;
      co_return 17;
   };

   auto account = forge::cli::command{};
   account.name = "account";
   account.aliases = {"acct"};
   account.summary = "Account operations";
   account.options = {std::move(profile), std::move(local)};
   account.commands = {std::move(show)};

   auto app = forge::cli::application{};
   app.name = "forge-client";
   app.version = "2.4.0";
   app.summary = "Forge CLI test client";
   app.options = {std::move(endpoint), std::move(verbose)};
   app.commands = {std::move(account)};
   return app;
}

const forge::cli::parsed_value& require_option(const forge::cli::invocation& input, std::string_view name) {
   const auto* value = input.find_option(name);
   BOOST_REQUIRE(value != nullptr);
   return *value;
}

bool has_completion(const std::vector<forge::cli::completion_item>& values, std::string_view text) {
   return std::any_of(values.begin(), values.end(), [text](const auto& value) { return value.text == text; });
}

} // namespace

BOOST_AUTO_TEST_CASE(cli_parses_nested_aliases_typed_values_and_inherited_options) {
   auto capture = std::make_shared<dispatch_capture>();
   const auto app = make_application(capture);
   const auto arguments = std::vector<std::string_view>{
       "acct",  "--local", "before", "--profile", "prod",    "ls",   "42",  "--endpoint", "https://example.test",
       "--tag", "alpha",   "-t",     "beta",      "--ratio", "1.25", "-vv",
   };

   const auto outcome = forge::cli::parse(app, arguments);
   const auto* dispatch = std::get_if<forge::cli::dispatch_outcome>(&outcome);
   BOOST_REQUIRE(dispatch != nullptr);
   BOOST_REQUIRE_EQUAL(dispatch->input.command_path.size(), 2);
   BOOST_TEST(dispatch->input.command_path[0] == "account");
   BOOST_TEST(dispatch->input.command_path[1] == "show");
   BOOST_TEST(std::get<std::string>(require_option(dispatch->input, "endpoint").values.front()) ==
              "https://example.test");
   BOOST_TEST(std::get<std::string>(require_option(dispatch->input, "profile").values.front()) == "prod");
   BOOST_TEST(std::get<std::string>(require_option(dispatch->input, "local").values.front()) == "before");
   BOOST_TEST(require_option(dispatch->input, "verbose").occurrences == 2U);
   BOOST_TEST(require_option(dispatch->input, "tag").occurrences == 2U);
   BOOST_TEST(std::get<double>(require_option(dispatch->input, "ratio").values.front()) == 1.25);
   const auto* account_id = dispatch->input.find_argument("account-id");
   BOOST_REQUIRE(account_id != nullptr);
   BOOST_TEST(std::get<std::int64_t>(account_id->values.front()) == 42);
   BOOST_TEST(!capture->called);
}

BOOST_AUTO_TEST_CASE(cli_local_parent_option_does_not_parse_after_nested_command) {
   const auto app = make_application(std::make_shared<dispatch_capture>());
   const auto arguments = std::vector<std::string_view>{
       "account", "show", "42", "--endpoint", "https://example.test", "--local", "late",
   };
   BOOST_CHECK_THROW(static_cast<void>(forge::cli::parse(app, arguments)), forge::cli::exceptions::parse_failed);
}

BOOST_AUTO_TEST_CASE(cli_reports_missing_malformed_conflicting_and_custom_validation_failures) {
   const auto app = make_application(std::make_shared<dispatch_capture>());
   const auto missing = std::vector<std::string_view>{"account", "show", "42"};
   BOOST_CHECK_THROW(static_cast<void>(forge::cli::parse(app, missing)), forge::cli::exceptions::validation_failed);

   const auto malformed = std::vector<std::string_view>{
       "account", "show", "not-an-integer", "--endpoint", "https://example.test",
   };
   BOOST_CHECK_THROW(static_cast<void>(forge::cli::parse(app, malformed)), forge::cli::exceptions::parse_failed);

   const auto malformed_real = std::vector<std::string_view>{
       "account", "show", "42", "--endpoint", "https://example.test", "--ratio", "1.25suffix",
   };
   BOOST_CHECK_EXCEPTION(
       static_cast<void>(forge::cli::parse(app, malformed_real)), forge::cli::exceptions::parse_failed,
       [](const auto& error) { return error.message().find("trailing characters") != std::string::npos; });

   const auto out_of_range_real = std::vector<std::string_view>{
       "account", "show", "42", "--endpoint", "https://example.test", "--ratio", "1e9999",
   };
   BOOST_CHECK_EXCEPTION(static_cast<void>(forge::cli::parse(app, out_of_range_real)),
                         forge::cli::exceptions::parse_failed,
                         [](const auto& error) { return error.message().find("out of range") != std::string::npos; });

   const auto conflict = std::vector<std::string_view>{
       "account", "show", "42", "--endpoint", "https://example.test", "--format", "json", "--raw",
   };
   BOOST_CHECK_THROW(static_cast<void>(forge::cli::parse(app, conflict)), forge::cli::exceptions::validation_failed);

   const auto repeated_flag = std::vector<std::string_view>{
       "account", "show", "42", "--endpoint", "https://example.test", "--raw", "--raw",
   };
   BOOST_CHECK_THROW(static_cast<void>(forge::cli::parse(app, repeated_flag)),
                     forge::cli::exceptions::validation_failed);

   const auto custom = std::vector<std::string_view>{
       "account", "show", "-1", "--endpoint", "https://example.test",
   };
   BOOST_CHECK_THROW(static_cast<void>(forge::cli::parse(app, custom)), forge::cli::exceptions::validation_failed);

   const auto command_validation = std::vector<std::string_view>{
       "account", "show", "42", "--endpoint", "blocked",
   };
   BOOST_CHECK_THROW(static_cast<void>(forge::cli::parse(app, command_validation)),
                     forge::cli::exceptions::validation_failed);
}

BOOST_AUTO_TEST_CASE(cli_help_help_all_and_version_are_normal_outcomes) {
   auto capture = std::make_shared<dispatch_capture>();
   const auto app = make_application(capture);

   const auto help_arguments = std::vector<std::string_view>{"account", "show", "--help"};
   const auto help = forge::cli::parse(app, help_arguments);
   const auto* help_value = std::get_if<forge::cli::help_outcome>(&help);
   BOOST_REQUIRE(help_value != nullptr);
   BOOST_REQUIRE_EQUAL(help_value->command_path.size(), 2);
   BOOST_TEST(help_value->command_path[0] == "account");
   BOOST_TEST(help_value->command_path[1] == "show");
   BOOST_TEST(help_value->text.find("--endpoint") != std::string::npos);
   BOOST_TEST(help_value->text.find("--profile") != std::string::npos);

   const auto all_arguments = std::vector<std::string_view>{"--help-all"};
   const auto all = forge::cli::parse(app, all_arguments);
   BOOST_REQUIRE(std::get_if<forge::cli::help_outcome>(&all) != nullptr);
   BOOST_TEST(std::get<forge::cli::help_outcome>(all).expanded);

   const auto version_arguments = std::vector<std::string_view>{"account", "show", "--version"};
   const auto version = forge::cli::parse(app, version_arguments);
   const auto* version_value = std::get_if<forge::cli::version_outcome>(&version);
   BOOST_REQUIRE(version_value != nullptr);
   BOOST_TEST(version_value->text == "forge-client 2.4.0\n");
   BOOST_TEST(!capture->called);
}

BOOST_AUTO_TEST_CASE(cli_descriptor_and_completion_share_command_metadata) {
   const auto app = make_application(std::make_shared<dispatch_capture>());
   const auto descriptor = forge::cli::describe(app);
   BOOST_TEST(descriptor.name == "forge-client");
   BOOST_REQUIRE_EQUAL(descriptor.commands.size(), 1);
   BOOST_TEST(!descriptor.commands.front().executable);
   BOOST_TEST(descriptor.commands.front().commands.front().executable);
   const auto encoded = forge::codec::json::write(descriptor);
   BOOST_REQUIRE(encoded.ok());
   const auto decoded = forge::codec::json::read<forge::cli::application_descriptor>(encoded.text);
   BOOST_REQUIRE(decoded.ok());
   BOOST_CHECK(decoded.value == descriptor);

   const auto option_words = std::vector<std::string_view>{"account", "show", "--fo"};
   const auto option_values = forge::cli::complete(descriptor, option_words);
   BOOST_TEST(has_completion(option_values, "--format"));

   const auto inherited_words = std::vector<std::string_view>{"account", "show", "--pro"};
   const auto inherited_values = forge::cli::complete(descriptor, inherited_words);
   BOOST_TEST(has_completion(inherited_values, "--profile"));

   const auto value_words = std::vector<std::string_view>{"account", "show", "--format", "j"};
   const auto values = forge::cli::complete(descriptor, value_words);
   BOOST_TEST(has_completion(values, "json"));
   BOOST_TEST(!has_completion(values, "text"));

   const auto argument_words = std::vector<std::string_view>{"account", "show", "4"};
   const auto argument_values = forge::cli::complete(descriptor, argument_words);
   BOOST_TEST(has_completion(argument_values, "42"));
   BOOST_TEST(!has_completion(argument_values, "77"));
}

BOOST_AUTO_TEST_CASE(cli_runner_dispatches_once_and_routes_help_to_terminal) {
   auto capture = std::make_shared<dispatch_capture>();
   const auto app = make_application(capture);
   auto standard_output = std::string{};
   auto standard_error = std::string{};
   auto terminal = forge::cli::terminal{
       [&](std::string_view text) { standard_output += text; },
       [&](std::string_view text) { standard_error += text; },
   };

   const auto arguments = std::vector<std::string_view>{
       "account", "show", "42", "--endpoint", "https://example.test",
   };
   const auto result = forge::cli::run(app, arguments, terminal, {.handle_sigint = false, .handle_sigterm = false});
   BOOST_TEST(result == 17);
   BOOST_TEST(capture->called);
   BOOST_TEST(!capture->stop_requested);
   BOOST_TEST(standard_output.empty());
   BOOST_TEST(standard_error.empty());

   capture->called = false;
   const auto help_arguments = std::vector<std::string_view>{"account", "show", "--help"};
   const auto help_result =
       forge::cli::run(app, help_arguments, terminal, {.handle_sigint = false, .handle_sigterm = false});
   BOOST_TEST(help_result == 0);
   BOOST_TEST(!capture->called);
   BOOST_TEST(standard_output.find("--endpoint") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(cli_runner_honors_external_cancellation_before_dispatch) {
   auto capture = std::make_shared<dispatch_capture>();
   const auto app = make_application(capture);
   auto terminal = forge::cli::terminal{[](std::string_view) {}, [](std::string_view) {}};
   auto source = std::stop_source{};
   source.request_stop();
   const auto arguments = std::vector<std::string_view>{
       "account", "show", "42", "--endpoint", "https://example.test",
   };

   const auto result = forge::cli::run(
       app, arguments, terminal, {.handle_sigint = false, .handle_sigterm = false, .stop_token = source.get_token()});
   BOOST_TEST(result == 130);
   BOOST_TEST(!capture->called);
}

BOOST_AUTO_TEST_CASE(cli_runner_escapes_terminal_controls_in_parse_diagnostics) {
   const auto app = make_application(std::make_shared<dispatch_capture>());
   auto standard_error = std::string{};
   auto output = forge::cli::terminal{
       [](std::string_view) {},
       [&](std::string_view text) { standard_error += text; },
   };
   const auto hostile = std::string{"--unknown\x1b]52;c;Zm9yZ2U=\x07\nforged error \xc2\x9b"
                                    "31m\xe2\x80\xae"};
   const auto arguments = std::vector<std::string_view>{
       "account", "show", "42", "--endpoint", "https://example.test", hostile,
   };

   const auto result = forge::cli::run(app, arguments, output, {.handle_sigint = false, .handle_sigterm = false});

   BOOST_TEST(result == 2);
   BOOST_TEST(standard_error.find('\x1b') == std::string::npos);
   BOOST_TEST(standard_error.find('\x07') == std::string::npos);
   BOOST_TEST(standard_error.find('\n') == standard_error.size() - 1);
   BOOST_TEST(standard_error.find("\\x1b]52;c;Zm9yZ2U=\\x07\\x0aforged error \\u009b31m\\u202e") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(cli_rejects_ambiguous_public_descriptors_before_backend_parse) {
   auto app = make_application(std::make_shared<dispatch_capture>());
   app.options.push_back(app.options.front());
   const auto arguments = std::vector<std::string_view>{"--help"};
   BOOST_CHECK_THROW(static_cast<void>(forge::cli::parse(app, arguments)), forge::cli::exceptions::invalid_descriptor);
}

BOOST_AUTO_TEST_CASE(cli_translates_invalid_argv_and_handler_standard_exceptions) {
   auto output = forge::cli::terminal{[](std::string_view) {}, [](std::string_view) {}};
   auto app = forge::cli::application{};
   app.name = "failure-client";
   app.require_command = false;
   app.handler = [](const forge::cli::invocation&, std::stop_token) -> boost::asio::awaitable<int> {
      throw std::runtime_error{"backend failed"};
      co_return 0;
   };

   BOOST_CHECK_THROW((void)forge::cli::run(app, {}, output, {.handle_sigint = false, .handle_sigterm = false}),
                     forge::cli::exceptions::dispatch_failed);
   BOOST_CHECK_THROW((void)forge::cli::run(app, 1, nullptr, {}), forge::cli::exceptions::invalid_arguments);
}
