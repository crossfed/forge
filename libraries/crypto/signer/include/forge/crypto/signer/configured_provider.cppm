module;

#include <boost/asio/awaitable.hpp>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

export module forge.crypto.signer.configured_provider;

export import forge.crypto.core.secret_string;
export import forge.crypto.signer.provider;

export namespace forge::crypto::signer {

struct private_key_file_options {
   std::size_t max_bytes = 4096;
};

struct configured_provider_options {
   key_id id;
   std::optional<core::secret_string> private_key;
   std::optional<std::filesystem::path> private_key_file;
   private_key_file_options file;
};

class configured_provider final : public provider {
 public:
   [[nodiscard]] static std::shared_ptr<configured_provider>
   create(configured_provider_options options, const asymmetric::encoding& encoding = asymmetric::encoding::forge());

   [[nodiscard]] static std::shared_ptr<configured_provider>
   from_private_key(key_id id, core::secret_string private_key,
                    const asymmetric::encoding& encoding = asymmetric::encoding::forge());

   [[nodiscard]] static std::shared_ptr<configured_provider>
   from_private_key_file(key_id id, const std::filesystem::path& path, private_key_file_options options = {},
                         const asymmetric::encoding& encoding = asymmetric::encoding::forge());

   ~configured_provider() override;

   configured_provider(const configured_provider&) = delete;
   configured_provider& operator=(const configured_provider&) = delete;

   [[nodiscard]] boost::asio::awaitable<std::vector<key_info>> keys() override;
   [[nodiscard]] boost::asio::awaitable<key_info> describe(const key_id& id) override;
   [[nodiscard]] boost::asio::awaitable<sign_digest_response> sign_digest(sign_digest_request request) override;

 private:
   struct impl;

   explicit configured_provider(std::unique_ptr<impl> implementation);

   std::unique_ptr<impl> impl_;
};

} // namespace forge::crypto::signer
