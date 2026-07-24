module;

#include <boost/describe.hpp>

#include <optional>
#include <vector>

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

class verified_quorum_certificate {
 public:
   verified_quorum_certificate(const verified_quorum_certificate&) = default;
   verified_quorum_certificate(verified_quorum_certificate&&) = default;
   verified_quorum_certificate&
   operator=(const verified_quorum_certificate&) = default;
   verified_quorum_certificate&
   operator=(verified_quorum_certificate&&) = default;

   [[nodiscard]] const quorum_certificate& get() const noexcept;
   [[nodiscard]] bool
   has_strong_vote(const forge::crypto::bls::public_key& finalizer) const noexcept;

 private:
   verified_quorum_certificate(
       quorum_certificate certificate,
       std::vector<forge::crypto::bls::public_key> strong_voters);

   quorum_certificate certificate_;
   std::vector<forge::crypto::bls::public_key> strong_voters_;

   friend verified_quorum_certificate
   verify(quorum_certificate, const verified_finalizer_policy&,
          const std::optional<verified_finalizer_policy>&, digest);
};

void verify_basic(const qc_signature& value, const verified_finalizer_policy& policy);
void verify_weights(const qc_signature& value, const verified_finalizer_policy& policy);
void verify_signature(const qc_signature& value, const verified_finalizer_policy& policy, digest strong_digest);
[[nodiscard]] verified_quorum_certificate
verify(quorum_certificate value, const verified_finalizer_policy& active,
       const std::optional<verified_finalizer_policy>& pending,
       digest finality_digest);

BOOST_DESCRIBE_STRUCT(qc_signature, (), (strong_votes, weak_votes, signature))
BOOST_DESCRIBE_STRUCT(quorum_certificate, (), (block, active, pending))

} // namespace forge::chain::savanna
