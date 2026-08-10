module;

#include <functional>
#include <string_view>

export module forge.cli.terminal;

export namespace forge::cli {

struct terminal_capabilities {
   bool output_is_terminal = false;
   bool error_is_terminal = false;
   bool color = false;
};

using terminal_writer = std::function<void(std::string_view)>;

class terminal {
 public:
   terminal(terminal_writer output, terminal_writer error, terminal_capabilities capabilities = {});

   void write(std::string_view text) const;
   void write_error(std::string_view text) const;
   [[nodiscard]] terminal_capabilities capabilities() const noexcept;

 private:
   terminal_writer output_;
   terminal_writer error_;
   terminal_capabilities capabilities_;
};

[[nodiscard]] terminal standard_terminal();

} // namespace forge::cli
