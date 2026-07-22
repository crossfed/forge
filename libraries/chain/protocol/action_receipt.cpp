module;

#include <forge/raw/serialization.hpp>

#include <new>

module forge.chain.protocol.action_receipt;

import forge.crypto.digest.sha256;
import forge.raw.datastream;
import forge.raw.raw;
import forge.raw.varint;
import forge.variant.value;
import forge.variant.conversion;
import forge.variant.containers;
import forge.variant.described;

namespace forge::chain::protocol {

core::digest calculate_savanna_witness_hash(const action_receipt& receipt) {
   auto encoder = core::digest::encoder{};
   forge::raw::pack(encoder, receipt.global_sequence);
   forge::raw::pack(encoder, receipt.auth_sequence);
   forge::raw::pack(encoder, receipt.code_sequence);
   forge::raw::pack(encoder, receipt.abi_sequence);
   return encoder.result();
}

core::digest calculate_savanna_action_digest(const action_receipt& receipt, const action& executed_action) {
   auto encoder = core::digest::encoder{};
   forge::raw::pack(encoder, receipt.receiver);
   forge::raw::pack(encoder, receipt.recv_sequence);
   forge::raw::pack(encoder, executed_action.account);
   forge::raw::pack(encoder, executed_action.name);
   forge::raw::pack(encoder, receipt.act_digest);
   forge::raw::pack(encoder, calculate_savanna_witness_hash(receipt));
   return encoder.result();
}

} // namespace forge::chain::protocol

FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::protocol::action_receipt)
