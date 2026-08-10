module;

#include <boost/asio/redirect_error.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/error_code.hpp>
#include <forge/exceptions/macros.hpp>

#include <csignal>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

module forge.cli.runner;

import forge.asio.blocking;
import forge.asio.exceptions;
import forge.asio.runtime;
import forge.cli.command;
import forge.cli.exceptions;
import forge.cli.parser;
import forge.cli.terminal;
import forge.core.utf8;

namespace forge::cli {
namespace {

struct cancellation_state {
   std::stop_source source;
};

bool is_terminal_control(std::uint32_t code_point) {
   return code_point <= 0x1fU || (code_point >= 0x7fU && code_point <= 0x9fU) || code_point == 0x061cU ||
          (code_point >= 0x200bU && code_point <= 0x200fU) || (code_point >= 0x2028U && code_point <= 0x202eU) ||
          (code_point >= 0x2060U && code_point <= 0x206fU) || code_point == 0xfeffU;
}

std::pair<std::uint32_t, std::size_t> decode_utf8(std::string_view text, std::size_t offset) {
   const auto first = static_cast<unsigned char>(text[offset]);
   if (first < 0x80U) {
      return {first, 1};
   }

   const auto length = first < 0xe0U ? 2U : first < 0xf0U ? 3U : 4U;
   auto code_point = static_cast<std::uint32_t>(first & (0x7fU >> length));
   for (auto index = std::size_t{1}; index < length; ++index) {
      code_point = (code_point << 6U) | (static_cast<unsigned char>(text[offset + index]) & 0x3fU);
   }
   return {code_point, length};
}

void append_hex_escape(std::string& output, std::uint32_t value, std::size_t digits) {
   constexpr auto hex_digits = std::string_view{"0123456789abcdef"};
   output += digits == 2 ? "\\x" : digits == 4 ? "\\u" : "\\U";
   for (auto shift = digits * 4U; shift != 0; shift -= 4U) {
      output += hex_digits[(value >> (shift - 4U)) & 0x0fU];
   }
}

std::string sanitize_diagnostic(std::string_view text) {
   const auto utf8 = forge::prune_invalid_utf8(text);
   auto result = std::string{};
   result.reserve(utf8.size());
   for (auto offset = std::size_t{0}; offset < utf8.size();) {
      const auto [code_point, length] = decode_utf8(utf8, offset);
      if (is_terminal_control(code_point)) {
         append_hex_escape(result, code_point, code_point <= 0xffU ? 2U : code_point <= 0xffffU ? 4U : 8U);
      } else {
         result.append(utf8, offset, length);
      }
      offset += length;
   }
   return result;
}

void write_line(terminal& output, std::string text, bool error) {
   if (text.empty() || text.back() != '\n') {
      text += '\n';
   }
   if (error) {
      output.write_error(text);
   } else {
      output.write(text);
   }
}

int usage_failure(terminal& output, const forge::exceptions::base& error, int exit_code) {
   write_line(output, "error: " + sanitize_diagnostic(error.message()), true);
   return exit_code;
}

} // namespace

boost::asio::awaitable<int> async_run(const application& app, std::span<const std::string_view> arguments,
                                      terminal& output, run_options options) {
   auto outcome = parse_outcome{};
   try {
      outcome = parse(app, arguments);
   } catch (const exceptions::parse_failed& error) {
      co_return usage_failure(output, error, options.usage_error_exit_code);
   } catch (const exceptions::validation_failed& error) {
      co_return usage_failure(output, error, options.usage_error_exit_code);
   } catch (const exceptions::dispatch_failed& error) {
      co_return usage_failure(output, error, options.usage_error_exit_code);
   }

   if (const auto* help = std::get_if<help_outcome>(&outcome)) {
      write_line(output, help->text, false);
      co_return 0;
   }
   if (const auto* version = std::get_if<version_outcome>(&outcome)) {
      write_line(output, version->text, false);
      co_return 0;
   }

   auto dispatch = std::get<dispatch_outcome>(std::move(outcome));
   auto cancellation = std::make_shared<cancellation_state>();
   if (options.stop_token.stop_requested()) {
      cancellation->source.request_stop();
   }
   auto external_stop = std::optional<std::stop_callback<std::function<void()>>>{};
   if (options.stop_token.stop_possible()) {
      external_stop.emplace(options.stop_token, [cancellation] { cancellation->source.request_stop(); });
   }

   const auto executor = co_await boost::asio::this_coro::executor;
   auto signals = boost::asio::signal_set{executor};
   if (options.handle_sigint) {
      signals.add(SIGINT);
   }
   if (options.handle_sigterm) {
      signals.add(SIGTERM);
   }
   if (options.handle_sigint || options.handle_sigterm) {
      signals.async_wait([cancellation](const boost::system::error_code& error, int) {
         if (!error) {
            cancellation->source.request_stop();
         }
      });
   }

   auto exit_code = 0;
   try {
      if (cancellation->source.stop_requested()) {
         co_return options.canceled_exit_code;
      }
      exit_code = co_await dispatch.handler(dispatch.input, cancellation->source.get_token());
   } catch (const exceptions::canceled&) {
      exit_code = options.canceled_exit_code;
   } catch (const forge::asio::exceptions::canceled&) {
      exit_code = options.canceled_exit_code;
   } catch (const forge::exceptions::base&) {
      throw;
   } catch (const std::exception& error) {
      FORGE_THROW_EXCEPTION(exceptions::dispatch_failed, "CLI command handler failed",
                            forge::exceptions::ctx("reason", error.what()));
   } catch (...) {
      FORGE_THROW_EXCEPTION(exceptions::dispatch_failed, "CLI command handler failed");
   }

   auto ignored = boost::system::error_code{};
   signals.cancel(ignored);
   if (cancellation->source.stop_requested()) {
      co_return options.canceled_exit_code;
   }
   co_return exit_code;
}

int run(const application& app, std::span<const std::string_view> arguments, terminal& output, run_options options) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1, .thread_name = "forge-cli"}};
   return forge::asio::blocking::run(runtime, async_run(app, arguments, output, std::move(options)));
}

int run(const application& app, int argc, const char* const argv[], run_options options) {
   if (argc < 0 || (argc > 0 && argv == nullptr)) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_arguments, "invalid CLI argc/argv");
   }
   auto arguments = std::vector<std::string_view>{};
   arguments.reserve(argc > 0 ? static_cast<std::size_t>(argc - 1) : 0);
   for (auto index = 1; index < argc; ++index) {
      arguments.emplace_back(argv[index] == nullptr ? "" : argv[index]);
   }
   auto output = standard_terminal();
   return run(app, arguments, output, std::move(options));
}

} // namespace forge::cli
