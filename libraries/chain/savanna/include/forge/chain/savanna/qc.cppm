module;

#include <boost/describe.hpp>

#include <optional>

export module forge.chain.savanna.qc;

export import forge.chain.savanna.finality_core;
export import forge.chain.savanna.policy;

export namespace forge::chain::savanna {

struct qc_signature {
   std::optional<vote_bitset> strong_votes;
   std::optional<vote_bitset> weak_votes;
   forge::crypto::bls::aggregate_signature signature;

   [[nodiscard]] bool weak() const noexcept;
   [[nodiscard]] bool strong() const noexcept;
};

struct quorum_certificate {
   block_num_t block = 0;
   qc_signature active;
   std::optional<qc_signature> pending;

   [[nodiscard]] bool strong() const noexcept;
   [[nodiscard]] qc_claim claim() const noexcept;
};

void verify_basic(const qc_signature& value, const verified_finalizer_policy& policy);
void verify_weights(const qc_signature& value, const verified_finalizer_policy& policy);
void verify_signature(const qc_signature& value, const verified_finalizer_policy& policy, digest strong_digest);
void verify(const quorum_certificate& value, const verified_finalizer_policy& active,
            const std::optional<verified_finalizer_policy>& pending, digest finality_digest);

BOOST_DESCRIBE_STRUCT(qc_signature, (), (strong_votes, weak_votes, signature))
BOOST_DESCRIBE_STRUCT(quorum_certificate, (), (block, active, pending))

} // namespace forge::chain::savanna
