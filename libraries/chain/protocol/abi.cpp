module;

#include <forge/raw/serialization.hpp>

#include <new>

module forge.chain.protocol.abi;

import forge.crypto.digest.sha256;
import forge.raw.datastream;
import forge.raw.raw;
import forge.variant.value;
import forge.variant.conversion;
import forge.variant.containers;
import forge.variant.chrono;
import forge.variant.multiprecision;
import forge.variant.format;
import forge.variant.described;

FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::protocol::type_def)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::protocol::field_def)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::protocol::struct_def)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::protocol::action_def)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::protocol::table_def)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::protocol::clause_pair)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::protocol::error_message)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::protocol::variant_def)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::protocol::action_result_def)
