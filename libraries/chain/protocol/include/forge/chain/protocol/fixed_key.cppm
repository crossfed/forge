export module forge.chain.protocol.fixed_key;

export import :value;

#if !defined(FORGE_CONTRACT_GUEST)
export import :variant;
#endif

export namespace forge::chain::protocol {

using key256 = fixed_key<32>;

} // namespace forge::chain::protocol
