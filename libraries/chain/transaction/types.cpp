module;

#include <forge/raw/serialization.hpp>

#include <new>

module forge.chain.transaction.types;

import forge.chain.protocol.transaction;
import forge.crypto.asymmetric;
import forge.crypto.digest.sha256;
import forge.raw.datastream;
import forge.raw.raw;
import forge.raw.varint;
import forge.variant.chrono;
import forge.variant.containers;
import forge.variant.conversion;
import forge.variant.described;
import forge.variant.format;
import forge.variant.multiprecision;
import forge.variant.value;

FORGE_IMPLEMENT_SERIALIZATION(forge::chain::transaction::unsigned_transaction)
FORGE_IMPLEMENT_SERIALIZATION(forge::chain::transaction::prepared_transaction)
