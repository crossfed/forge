#include <boost/asio/awaitable.hpp>

#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

import forge.cli.command;
import forge.cli.runner;
import forge.cli.terminal;

int main() {
   auto called = false;
   auto endpoint = forge::cli::option{};
   endpoint.name = "endpoint";
   endpoint.required = true;

   auto name = forge::cli::argument{};
   name.name = "name";

   auto ping = forge::cli::command{};
   ping.name = "ping";
   ping.arguments = {std::move(name)};
   ping.handler = [&](const forge::cli::invocation& input, std::stop_token) -> boost::asio::awaitable<int> {
      called = input.command_path == std::vector<std::string>({"node", "ping"}) &&
               input.find_option("endpoint") != nullptr && input.find_argument("name") != nullptr;
      co_return called ? 0 : 1;
   };

   auto node = forge::cli::command{};
   node.name = "node";
   node.commands = {std::move(ping)};

   auto app = forge::cli::application{};
   app.name = "package-client";
   app.options = {std::move(endpoint)};
   app.commands = {std::move(node)};

   auto output_text = std::string{};
   auto error_text = std::string{};
   auto output = forge::cli::terminal{
       [&](std::string_view text) { output_text += text; },
       [&](std::string_view text) { error_text += text; },
   };
   const auto arguments = std::vector<std::string_view>{
       "node", "ping", "peer-a", "--endpoint", "https://example.test",
   };
   const auto result = forge::cli::run(app, arguments, output, {.handle_sigint = false, .handle_sigterm = false});
   return result == 0 && called && output_text.empty() && error_text.empty() ? 0 : 1;
}
