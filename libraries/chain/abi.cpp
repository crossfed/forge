module;

#include <forge/raw/serialization.hpp>

#include <new>

module forge.chain.abi;

import forge.crypto.sha256;
import forge.raw.datastream;
import forge.raw.raw;
import forge.variant.value;
import forge.variant.conversion;
import forge.variant.containers;
import forge.variant.chrono;
import forge.variant.multiprecision;
import forge.variant.format;
import forge.variant.described;

FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::type_def)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::field_def)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::struct_def)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::action_def)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::table_def)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::clause_pair)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::error_message)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::variant_def)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::action_result_def)
