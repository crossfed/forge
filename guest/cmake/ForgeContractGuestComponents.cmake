# Guest-compatible Forge leaves available to downstream contract libraries.
# The including build defines forge_contract_register_guest_component().

forge_contract_register_guest_component(
   ID forge.raw
   TARGET forge_raw
   ARCHIVE libforge_guest_raw.a
   MODULES
      forge/raw/stream.cppm
      forge/raw/varint_value.cppm
      forge/raw/codec.cppm
   MODULE_NAMES
      forge.raw.stream
      forge.raw.varint_value
      forge.raw.codec
)

forge_contract_register_guest_component(
   ID forge.codec.base64
   TARGET forge_codec_base64
   ARCHIVE libforge_guest_codec_base64.a
   MODULES
      forge/codec/base64/exceptions.cppm
      forge/codec/base64/base64.cppm
   MODULE_NAMES
      forge.codec.base64.exceptions
      forge.codec.base64
)

forge_contract_register_guest_component(
   ID forge.codec.base58
   TARGET forge_codec_base58
   ARCHIVE libforge_guest_codec_base58.a
   MODULES
      forge/codec/base58/exceptions.cppm
      forge/codec/base58/base58.cppm
   MODULE_NAMES
      forge.codec.base58.exceptions
      forge.codec.base58
)

forge_contract_register_guest_component(
   ID forge.codec.hex
   TARGET forge_codec_hex
   ARCHIVE libforge_guest_codec_hex.a
   MODULES
      forge/codec/hex/exceptions.cppm
      forge/codec/hex/hex.cppm
   MODULE_NAMES
      forge.codec.hex.exceptions
      forge.codec.hex
)

forge_contract_register_guest_component(
   ID forge.crypto.digest
   TARGET forge_crypto_digest
   PUBLIC_LIBRARIES forge.raw
   MODULES
      forge/crypto/digest/sha256_value.cppm
      forge/crypto/digest/sha256.cppm
      forge/crypto/digest/sha512_value.cppm
      forge/crypto/digest/sha512.cppm
      forge/crypto/digest/ripemd160_value.cppm
      forge/crypto/digest/ripemd160.cppm
   MODULE_NAMES
      forge.crypto.digest.sha256:value
      forge.crypto.digest.sha256
      forge.crypto.digest.sha512:value
      forge.crypto.digest.sha512
      forge.crypto.digest.ripemd160:value
      forge.crypto.digest.ripemd160
)

forge_contract_register_guest_component(
   ID forge.crypto.asymmetric_values
   TARGET forge_crypto_asymmetric_values
   PUBLIC_LIBRARIES forge.raw
   MODULES forge/crypto/asymmetric/values.cppm
   MODULE_NAMES forge.crypto.asymmetric.values
)

forge_contract_register_guest_component(
   ID forge.crypto.asymmetric
   TARGET forge_crypto_asymmetric
   PUBLIC_LIBRARIES forge.crypto.asymmetric_values
   MODULES forge/crypto/asymmetric/asymmetric.cppm
   MODULE_NAMES forge.crypto.asymmetric
)

forge_contract_register_guest_component(
   ID forge.crypto.bls_values
   TARGET forge_crypto_bls_values
   PUBLIC_LIBRARIES forge.raw
   MODULES forge/crypto/bls/bls_values.cppm
   MODULE_NAMES forge.crypto.bls.values
)

forge_contract_register_guest_component(
   ID forge.chain.savanna.values
   TARGET forge_chain_savanna_values
   PUBLIC_LIBRARIES
      forge.raw
      forge.crypto.bls_values
   MODULES forge/chain/savanna/values.cppm
   MODULE_NAMES forge.chain.savanna.values
)

forge_contract_register_guest_component(
   ID forge.chain.protocol
   TARGET forge_chain_protocol
   ARCHIVE libforge_guest_chain_protocol.a
   PUBLIC_LIBRARIES
      forge.raw
      forge.crypto.digest
      forge.crypto.asymmetric
      forge.crypto.bls_values
      forge.chain.savanna.values
   MODULES
      forge/chain/protocol/values.cppm
      forge/chain/protocol/time.cppm
      forge/chain/protocol/types_value.cppm
      forge/chain/protocol/types.cppm
      forge/chain/protocol/typed_id.cppm
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
      forge/chain/protocol/producer_authority_value.cppm
      forge/chain/protocol/producer_authority.cppm
      forge/chain/protocol/system_value.cppm
      forge/chain/protocol/system.cppm
      forge/chain/protocol/code_hash_result.cppm
      forge/chain/protocol/blockchain_parameters.cppm
      forge/chain/protocol/kv_parameters.cppm
      forge/chain/protocol/finalizer_authority_value.cppm
      forge/chain/protocol/finalizer_authority.cppm
      forge/chain/protocol/finalizer_policy_value.cppm
      forge/chain/protocol/finalizer_policy.cppm
      forge/chain/protocol/hash_id.cppm
      forge/chain/protocol/call_access_mode.cppm
      forge/chain/protocol/call_data_header.cppm
   MODULE_NAMES
      forge.chain.protocol.values
      forge.chain.protocol.time
      forge.chain.protocol.types:value
      forge.chain.protocol.types
      forge.chain.protocol.typed_id
      forge.chain.protocol.fixed_key:value
      forge.chain.protocol.fixed_key
      forge.chain.protocol.action:value
      forge.chain.protocol.action
      forge.chain.protocol.transaction:value
      forge.chain.protocol.transaction
      forge.chain.protocol.authority:value
      forge.chain.protocol.authority
      forge.chain.protocol.producer_schedule:value
      forge.chain.protocol.producer_schedule
      forge.chain.protocol.producer_authority:value
      forge.chain.protocol.producer_authority
      forge.chain.protocol.system:value
      forge.chain.protocol.system
      forge.chain.protocol.code_hash_result
      forge.chain.protocol.blockchain_parameters
      forge.chain.protocol.kv_parameters
      forge.chain.protocol.finalizer_authority:value
      forge.chain.protocol.finalizer_authority
      forge.chain.protocol.finalizer_policy:value
      forge.chain.protocol.finalizer_policy
      forge.chain.protocol.hash_id
      forge.chain.protocol.call_access_mode
      forge.chain.protocol.call_data_header
)

forge_contract_register_guest_component(
   FOUNDATION
   ID forge.contract.runtime
   TARGET forge_contract_runtime
   ARCHIVE libforge_guest_contract.a
   PUBLIC_LIBRARIES
      forge.raw
      forge.codec.base64
      forge.codec.base58
      forge.codec.hex
      forge.chain.protocol
   MODULES
      forge/contract/intrinsics.cppm
      forge/contract/contract.cppm
      forge/contract/datastream.cppm
      forge/contract/varint.cppm
      forge/contract/fixed_bytes.cppm
      forge/contract/binary_extension.cppm
      forge/contract/ignore.cppm
      forge/contract/hash_id.cppm
      forge/contract/action.cppm
      forge/contract/transaction.cppm
      forge/contract/system.cppm
      forge/contract/deferred_transaction.cppm
      forge/contract/authorization.cppm
      forge/contract/bitset.cppm
      forge/contract/call.cppm
      forge/contract/crypto.cppm
      forge/contract/crypto_bls_ext.cppm
      forge/contract/crypto_ext.cppm
      forge/contract/instant_finality.cppm
      forge/contract/key.cppm
      forge/contract/powers.cppm
      forge/contract/print.cppm
      forge/contract/privileged.cppm
      forge/contract/producer_schedule.cppm
      forge/contract/rope.cppm
      forge/contract/string.cppm
      forge/contract/dispatcher.cppm
      forge/contract/multi_index.cppm
      forge/contract/singleton.cppm
      forge/contract/compatibility_name.cppm
      forge/contract/compatibility_asset.cppm
   MODULE_NAMES
      forge.contract.intrinsics
      forge.contract
      forge.contract.datastream
      forge.contract.varint
      forge.contract.fixed_bytes
      forge.contract.binary_extension
      forge.contract.ignore
      forge.contract.hash_id
      forge.contract.action
      forge.contract.transaction
      forge.contract.system
      forge.contract.deferred_transaction
      forge.contract.authorization
      forge.contract.bitset
      forge.contract.call
      forge.contract.crypto
      forge.contract.crypto_bls_ext
      forge.contract.crypto_ext
      forge.contract.instant_finality
      forge.contract.key
      forge.contract.powers
      forge.contract.print
      forge.contract.privileged
      forge.contract.producer_schedule
      forge.contract.rope
      forge.contract.string
      forge.contract.dispatcher
      forge.contract.multi_index
      forge.contract.singleton
      forge.contract.compatibility_name
      forge.contract.compatibility_asset
)
