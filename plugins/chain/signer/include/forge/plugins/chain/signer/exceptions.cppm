module;

#include <forge/exceptions/macros.hpp>

#include <cstdint>

export module forge.plugins.chain.signer.exceptions;

export import forge.exceptions;

export namespace forge::plugins::chain::signer::exceptions {

enum class code : std::uint16_t {
   invalid_config = 1,
   invalid_lifecycle = 2,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.plugins.chain.signer")

using invalid_config = forge::exceptions::coded_exception<code, code::invalid_config>;
using invalid_lifecycle = forge::exceptions::coded_exception<code, code::invalid_lifecycle>;

} // namespace forge::plugins::chain::signer::exceptions
