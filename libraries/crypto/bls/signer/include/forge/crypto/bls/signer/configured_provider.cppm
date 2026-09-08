module;

#include <boost/asio/awaitable.hpp>

#include <memory>
#include <vector>

export module forge.crypto.bls.signer.configured_provider;

export import forge.crypto.bls.signer.provider;

export namespace forge::crypto::bls::signer {

class configured_provider final : public provider {
 public:
   [[nodiscard]] static std::shared_ptr<configured_provider> create(private_key&& value);

   ~configured_provider() override;

   configured_provider(const configured_provider&) = delete;
   configured_provider& operator=(const configured_provider&) = delete;

   [[nodiscard]] boost::asio::awaitable<description> describe() override;
   [[nodiscard]] boost::asio::awaitable<sign_result> sign(std::vector<std::uint8_t> message) override;

 private:
   struct impl;

   explicit configured_provider(std::unique_ptr<impl> implementation);

   std::unique_ptr<impl> impl_;
};

} // namespace forge::crypto::bls::signer
