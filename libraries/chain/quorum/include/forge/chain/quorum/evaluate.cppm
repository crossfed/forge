module;

#include <cstdint>
#include <span>

export module forge.chain.quorum.evaluate;

export import forge.chain.quorum.types;
export import forge.chain.quorum.exceptions;

export namespace forge::chain::quorum {

[[nodiscard]] result evaluate(std::uint64_t threshold, std::span<const std::uint64_t> weights,
                              std::span<const std::uint32_t> signer_indices);

} // namespace forge::chain::quorum
