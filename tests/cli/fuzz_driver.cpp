#include <boost/asio/awaitable.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

import forge.cli.command;
import forge.cli.parser;
import forge.exceptions;

namespace {

forge::cli::application make_application() {
   auto format = forge::cli::option{};
   format.name = "format";
   format.aliases = {"-f"};
   format.completion_values = {"json", "text"};

   auto verbose = forge::cli::option{};
   verbose.name = "verbose";
   verbose.aliases = {"-v"};
   verbose.form = forge::cli::option_form::flag;
   verbose.repeatable = true;

   auto id = forge::cli::argument{};
   id.name = "id";
   id.type = forge::cli::value_kind::integer;

   auto show = forge::cli::command{};
   show.name = "show";
   show.options = {std::move(format)};
   show.arguments = {std::move(id)};
   show.handler = [](const forge::cli::invocation&, std::stop_token) -> boost::asio::awaitable<int> { co_return 0; };

   auto account = forge::cli::command{};
   account.name = "account";
   account.commands = {std::move(show)};

   auto app = forge::cli::application{};
   app.name = "forge-cli-fuzz";
   app.version = "1";
   app.options = {std::move(verbose)};
   app.commands = {std::move(account)};
   return app;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
   constexpr auto maximum_input_bytes = std::size_t{4'096};
   constexpr auto maximum_arguments = std::size_t{64};
   if (size > maximum_input_bytes) {
      return 0;
   }

   auto storage = std::vector<std::string>{};
   storage.emplace_back();
   for (const auto value : std::span{data, size}) {
      if (value == 0U && storage.size() < maximum_arguments) {
         storage.emplace_back();
      } else {
         storage.back().push_back(static_cast<char>(value));
      }
   }
   auto arguments = std::vector<std::string_view>{};
   arguments.reserve(storage.size());
   for (const auto& value : storage) {
      arguments.emplace_back(value);
   }

   try {
      static_cast<void>(forge::cli::parse(make_application(), arguments));
   } catch (const forge::exceptions::base&) {
      // Rejected descriptors and argv are expected fuzz inputs.
   }
   return 0;
}
