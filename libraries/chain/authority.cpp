module;

#include <forge/raw/serialization.hpp>

#include <new>

module forge.chain.authority;

import forge.crypto.sha256;
import forge.raw.datastream;
import forge.raw.raw;
import forge.variant.value;
import forge.variant.conversion;
import forge.variant.containers;
import forge.variant.format;
import forge.variant.described;

FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::permission_level_weight)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::key_weight)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::wait_weight)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::authority)
