module;

#include <cstdint>
#include <forge/exceptions/macros.hpp>

export module forge.chain.fork.exceptions;

export import forge.exceptions;

export namespace forge::chain::fork::exceptions {

enum class code : std::uint16_t {
   empty_graph = 1,
   unknown_node = 2,
   invalid_root = 3,
   invalid_node = 4,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.chain.fork")

using empty_graph = forge::exceptions::coded_exception<code, code::empty_graph>;
using unknown_node = forge::exceptions::coded_exception<code, code::unknown_node>;
using invalid_root = forge::exceptions::coded_exception<code, code::invalid_root>;
using invalid_node = forge::exceptions::coded_exception<code, code::invalid_node>;

} // namespace forge::chain::fork::exceptions
