#pragma once

namespace forge::plugins::chain::signer {

class plugin::finality_api_impl final : public forge::chain::api::finality_signer {
 public:
   explicit finality_api_impl(std::shared_ptr<impl> state);

   boost::asio::awaitable<forge::chain::api::finalizer_identity> identity() override;
   boost::asio::awaitable<forge::chain::savanna::finalizer_vote>
   sign_vote(forge::chain::savanna::block_ref block, forge::chain::savanna::vote_kind kind) override;

 private:
   std::shared_ptr<impl> state_;
};

} // namespace forge::plugins::chain::signer
