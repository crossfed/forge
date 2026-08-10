module;

#include <boost/asio/awaitable.hpp>

#include <span>
#include <stop_token>
#include <string_view>

export module forge.cli.runner;

import forge.cli.command;
import forge.cli.terminal;

export namespace forge::cli {

struct run_options {
   bool handle_sigint = true;
   bool handle_sigterm = true;
   std::stop_token stop_token;
   int usage_error_exit_code = 2;
   int canceled_exit_code = 130;
};

boost::asio::awaitable<int> async_run(const application& app, std::span<const std::string_view> arguments,
                                      terminal& output, run_options options = {});

int run(const application& app, std::span<const std::string_view> arguments, terminal& output,
        run_options options = {});

int run(const application& app, int argc, const char* const argv[], run_options options = {});

} // namespace forge::cli
