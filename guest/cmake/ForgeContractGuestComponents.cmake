# Guest-compatible Forge leaves available to downstream contract libraries.
# The including build defines forge_contract_register_guest_component().

forge_contract_register_guest_component(
   ID forge.raw
   ARCHIVE libforge_guest_raw.a
   MODULES
      forge/raw/stream.cppm
      forge/raw/varint_value.cppm
      forge/raw/codec.cppm
)

forge_contract_register_guest_component(
   ID forge.codec.base64
   ARCHIVE libforge_guest_codec_base64.a
   MODULES
      forge/codec/base64/exceptions.cppm
      forge/codec/base64/base64.cppm
)

forge_contract_register_guest_component(
   ID forge.codec.base58
   ARCHIVE libforge_guest_codec_base58.a
   MODULES
      forge/codec/base58/exceptions.cppm
      forge/codec/base58/base58.cppm
)

forge_contract_register_guest_component(
   ID forge.codec.hex
   ARCHIVE libforge_guest_codec_hex.a
   MODULES
      forge/codec/hex/exceptions.cppm
      forge/codec/hex/hex.cppm
)

forge_contract_register_guest_component(
   ID forge.crypto.digest
   PUBLIC_LIBRARIES forge.raw
   MODULES
      forge/crypto/digest/sha256_value.cppm
      forge/crypto/digest/sha256.cppm
      forge/crypto/digest/sha512_value.cppm
      forge/crypto/digest/sha512.cppm
      forge/crypto/digest/ripemd160_value.cppm
      forge/crypto/digest/ripemd160.cppm
)

forge_contract_register_guest_component(
   ID forge.crypto.asymmetric_values
   PUBLIC_LIBRARIES forge.raw
   MODULES forge/crypto/asymmetric/values.cppm
)

forge_contract_register_guest_component(
   ID forge.crypto.asymmetric
   PUBLIC_LIBRARIES forge.crypto.asymmetric_values
   MODULES forge/crypto/asymmetric/asymmetric.cppm
)

forge_contract_register_guest_component(
   ID forge.crypto.bls_values
   MODULES forge/crypto/bls/bls_values.cppm
)

forge_contract_register_guest_component(
   ID forge.chain.protocol
   ARCHIVE libforge_guest_chain_protocol.a
   PUBLIC_LIBRARIES
      forge.raw
      forge.crypto.digest
      forge.crypto.asymmetric
      forge.crypto.bls_values
   MODULES
      forge/chain/protocol/values.cppm
      forge/chain/protocol/time.cppm
      forge/chain/protocol/types_value.cppm
      forge/chain/protocol/types.cppm
      forge/chain/protocol/fixed_key_value.cppm
      forge/chain/protocol/fixed_key.cppm
      forge/chain/protocol/action_value.cppm
      forge/chain/protocol/action.cppm
      forge/chain/protocol/transaction_value.cppm
      forge/chain/protocol/transaction.cppm
      forge/chain/protocol/authority_value.cppm
      forge/chain/protocol/authority.cppm
      forge/chain/protocol/producer_schedule_value.cppm
      forge/chain/protocol/producer_schedule.cppm
      forge/chain/protocol/producer_authority.cppm
      forge/chain/protocol/system_value.cppm
      forge/chain/protocol/system.cppm
      forge/chain/protocol/code_hash_result.cppm
      forge/chain/protocol/blockchain_parameters.cppm
      forge/chain/protocol/kv_parameters.cppm
      forge/chain/protocol/finalizer_authority.cppm
      forge/chain/protocol/finalizer_policy.cppm
      forge/chain/protocol/hash_id.cppm
      forge/chain/protocol/call_access_mode.cppm
      forge/chain/protocol/call_data_header.cppm
)
