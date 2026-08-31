module;

export module forge.chain.savanna.admission;

export import forge.chain.savanna.candidate;
export import forge.chain.savanna.extensions;

export namespace forge::chain::savanna {

struct genesis_candidate {
   forge::chain::protocol::signed_block block;
   candidate value;
};

struct prepared_admission {
   block_id id;
   block_extensions block;
   header_extensions header;
   forge::chain::protocol::producer_authority producer;
   digest expected_action_root;
};

[[nodiscard]] genesis_candidate make_genesis_candidate(const genesis& value, state_commitment commitment);
[[nodiscard]] prepared_admission prepare(const candidate& parent, const forge::chain::protocol::signed_block& block);
void verify_merkle(const forge::chain::protocol::signed_block& block, const prepared_admission& prepared);
void verify_signature(const forge::chain::protocol::signed_block& block, const prepared_admission& prepared);
void verify_qc(const candidate& parent, const prepared_admission& prepared);
[[nodiscard]] candidate finish(const candidate& parent, const forge::chain::protocol::signed_block& block,
                               prepared_admission prepared);
[[nodiscard]] candidate admit(const candidate& parent, const forge::chain::protocol::signed_block& block);

} // namespace forge::chain::savanna
