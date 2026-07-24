#pragma once

namespace forge::plugins::crypto::signer {

class plugin::bls_api_impl final : public bls_api {
 public:
   explicit bls_api_impl(std::shared_ptr<impl> state);

   boost::asio::awaitable<bls_description> describe(bls_describe_request value) override;
   boost::asio::awaitable<bls_response> sign(bls_sign_request value) override;

 private:
   std::shared_ptr<impl> state_;
};

} // namespace forge::plugins::crypto::signer
