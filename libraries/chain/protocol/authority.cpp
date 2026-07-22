module;

#include <forge/raw/serialization.hpp>

#include <new>

module forge.chain.protocol.authority;

import forge.crypto.digest.sha256;
import forge.raw.datastream;
import forge.raw.raw;
import forge.variant.value;
import forge.variant.conversion;
import forge.variant.containers;
import forge.variant.format;
import forge.variant.described;

FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::protocol::permission_level_weight)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::protocol::key_weight)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::protocol::wait_weight)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::protocol::authority)
