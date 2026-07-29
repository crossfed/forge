module;

#include <cstdint>
#include <forge/exceptions/macros.hpp>

export module forge.contract.graph.exceptions;

export import forge.exceptions;

export namespace forge::contract::graph::exceptions {

enum class code : std::uint16_t {
   read = 1,
   invalid_descriptor = 2,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.contract.graph")

using read_error = forge::exceptions::coded_exception<code, code::read>;
using invalid_descriptor = forge::exceptions::coded_exception<code, code::invalid_descriptor>;

} // namespace forge::contract::graph::exceptions
