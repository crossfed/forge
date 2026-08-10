module;

#include <forge/exceptions/macros.hpp>

#include <exception>
#include <iostream>
#include <string_view>
#include <utility>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

module forge.cli.terminal;

import forge.cli.exceptions;

namespace forge::cli {
namespace {

bool is_terminal(int descriptor) noexcept {
#if defined(_WIN32)
   return ::_isatty(descriptor) != 0;
#else
   return ::isatty(descriptor) != 0;
#endif
}

void write_stream(std::ostream& stream, std::string_view text) {
   stream.write(text.data(), static_cast<std::streamsize>(text.size()));
   stream.flush();
   if (!stream) {
      FORGE_THROW_EXCEPTION(exceptions::terminal_failed, "terminal write failed");
   }
}

} // namespace

terminal::terminal(terminal_writer output, terminal_writer error, terminal_capabilities capabilities)
    : output_(std::move(output)), error_(std::move(error)), capabilities_(capabilities) {
   if (!output_ || !error_) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_descriptor, "terminal writers must not be empty");
   }
}

void terminal::write(std::string_view text) const {
   try {
      output_(text);
   } catch (const forge::exceptions::base&) {
      throw;
   } catch (const std::exception& error) {
      FORGE_THROW_EXCEPTION(exceptions::terminal_failed, "terminal output writer failed",
                            forge::exceptions::ctx("reason", error.what()));
   } catch (...) {
      FORGE_THROW_EXCEPTION(exceptions::terminal_failed, "terminal output writer failed");
   }
}

void terminal::write_error(std::string_view text) const {
   try {
      error_(text);
   } catch (const forge::exceptions::base&) {
      throw;
   } catch (const std::exception& error) {
      FORGE_THROW_EXCEPTION(exceptions::terminal_failed, "terminal error writer failed",
                            forge::exceptions::ctx("reason", error.what()));
   } catch (...) {
      FORGE_THROW_EXCEPTION(exceptions::terminal_failed, "terminal error writer failed");
   }
}

terminal_capabilities terminal::capabilities() const noexcept {
   return capabilities_;
}

terminal standard_terminal() {
   const auto capabilities = terminal_capabilities{
       .output_is_terminal = is_terminal(1),
       .error_is_terminal = is_terminal(2),
       .color = is_terminal(1) && is_terminal(2),
   };
   return terminal{
       [](std::string_view text) { write_stream(std::cout, text); },
       [](std::string_view text) { write_stream(std::cerr, text); },
       capabilities,
   };
}

} // namespace forge::cli
