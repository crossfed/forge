module;

#include <cstdint>
#include <forge/exceptions/macros.hpp>

export module forge.chain.quorum.exceptions;

export import forge.exceptions;

export namespace forge::chain::quorum::exceptions {

enum class code : std::uint16_t {
   duplicate_signer = 1,
   signer_out_of_range = 2,
   weight_overflow = 3,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.chain.quorum")

using duplicate_signer = forge::exceptions::coded_exception<code, code::duplicate_signer>;
using signer_out_of_range = forge::exceptions::coded_exception<code, code::signer_out_of_range>;
using weight_overflow = forge::exceptions::coded_exception<code, code::weight_overflow>;

} // namespace forge::chain::quorum::exceptions
