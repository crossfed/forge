module;

#include <boost/describe.hpp>

#include <cstdint>
#include <memory>
#include <optional>

export module forge.chain.savanna.vote_accumulator;

export import forge.chain.savanna.qc;
export import forge.chain.savanna.vote;

export namespace forge::chain::savanna {

enum class accumulator_state : std::uint8_t {
   unrestricted,
   restricted,
   weak_achieved,
   weak_final,
   strong,
};

struct policy_accumulator_status {
   accumulator_state state = accumulator_state::unrestricted;
   std::uint64_t strong_weight = 0;
   std::uint64_t weak_weight = 0;

   [[nodiscard]] bool quorum_reached() const noexcept;
};

struct accumulator_status {
   policy_accumulator_status active;
   std::optional<policy_accumulator_status> pending;

   [[nodiscard]] bool quorum_reached() const noexcept;
};

struct finalizer_vote_status {
   bool known = false;
   std::optional<vote_kind> active;
   std::optional<vote_kind> pending;
};

class vote_accumulator {
 public:
   vote_accumulator(block_ref candidate, verified_finalizer_policy active,
                    std::optional<verified_finalizer_policy> pending = std::nullopt);
   ~vote_accumulator();

   vote_accumulator(const vote_accumulator&) = delete;
   vote_accumulator& operator=(const vote_accumulator&) = delete;
   vote_accumulator(vote_accumulator&&) noexcept;
   vote_accumulator& operator=(vote_accumulator&&) noexcept;

   vote_result add(const finalizer_vote& vote);
   bool observe(const quorum_certificate& certificate);
   [[nodiscard]] std::optional<quorum_certificate> best() const;
   [[nodiscard]] accumulator_status status() const;
   [[nodiscard]] finalizer_vote_status
   status(const forge::crypto::bls::public_key& finalizer) const;

 private:
   struct impl;
   std::unique_ptr<impl> impl_;
};

BOOST_DESCRIBE_ENUM(accumulator_state, unrestricted, restricted, weak_achieved,
                    weak_final, strong)
BOOST_DESCRIBE_STRUCT(policy_accumulator_status, (), (state, strong_weight, weak_weight))
BOOST_DESCRIBE_STRUCT(accumulator_status, (), (active, pending))
BOOST_DESCRIBE_STRUCT(finalizer_vote_status, (), (known, active, pending))

} // namespace forge::chain::savanna
