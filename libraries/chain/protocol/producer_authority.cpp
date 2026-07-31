module;

#include <forge/raw/serialization.hpp>

#include <new>

module forge.chain.protocol.producer_authority;

import forge.crypto.asymmetric;
import forge.crypto.digest.sha256;
import forge.raw.datastream;
import forge.raw.raw;
import forge.variant.value;
import forge.variant.conversion;
import forge.variant.containers;
import forge.variant.format;
import forge.variant.described;
import forge.variant.static_variant;

FORGE_IMPLEMENT_SERIALIZATION(forge::chain::protocol::block_signing_authority_v0)
FORGE_IMPLEMENT_SERIALIZATION(forge::chain::protocol::producer_authority)
FORGE_IMPLEMENT_SERIALIZATION(forge::chain::protocol::producer_authority_schedule)
