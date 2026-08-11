module;

#include <forge/raw/serialization.hpp>

#include <new>

module forge.chain.savanna.values;

import forge.crypto.bls.serialization;
import forge.crypto.digest.sha256;
import forge.raw.datastream;
import forge.raw.raw;
import forge.variant.containers;
import forge.variant.conversion;
import forge.variant.described;
import forge.variant.format;
import forge.variant.value;

FORGE_IMPLEMENT_SERIALIZATION(forge::chain::savanna::finalizer)
FORGE_IMPLEMENT_SERIALIZATION(forge::chain::savanna::finalizer_policy)
