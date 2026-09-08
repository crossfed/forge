#pragma once

namespace forge::plugins::chain::signer {

class plugin::transaction_api_impl final : public forge::chain::api::transaction_signer {
 public:
   explicit transaction_api_impl(std::shared_ptr<impl> state);

   boost::asio::awaitable<forge::chain::transaction::prepared_transaction>
   sign(forge::chain::transaction::unsigned_transaction transaction,
        forge::api::auth::authenticated_caller caller) override;

 private:
   std::shared_ptr<impl> state_;
};

} // namespace forge::plugins::chain::signer
