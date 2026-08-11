module;

#include <forge/raw/serialization.hpp>

#include <new>

module forge.chain.protocol.finalizer_policy;

import forge.crypto.bls.serialization;
import forge.crypto.digest.sha256;
import forge.raw.datastream;
import forge.raw.raw;
import forge.variant.value;
import forge.variant.conversion;
import forge.variant.containers;
import forge.variant.format;
import forge.variant.described;

FORGE_IMPLEMENT_SERIALIZATION(forge::chain::protocol::finalizer_policy)
