module;

#include <cstdint>
#include <forge/exceptions/macros.hpp>

export module forge.blobdb.exceptions;

export import forge.exceptions;

export namespace forge::blobdb::exceptions {

enum class code : std::uint16_t {
   invalid_config = 1,
   not_found = 2,
   digest_mismatch = 3,
   transaction_closed = 4,
   unsupported_operation = 5,
};

FORGE_DECLARE_EXCEPTION_CATEGORY(code, "forge.blobdb")

using invalid_config = forge::exceptions::coded_exception<code, code::invalid_config>;
using not_found = forge::exceptions::coded_exception<code, code::not_found>;
using digest_mismatch = forge::exceptions::coded_exception<code, code::digest_mismatch>;
using transaction_closed = forge::exceptions::coded_exception<code, code::transaction_closed>;
using unsupported_operation = forge::exceptions::coded_exception<code, code::unsupported_operation>;

} // namespace forge::blobdb::exceptions
