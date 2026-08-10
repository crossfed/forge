module;

#include <boost/asio/awaitable.hpp>
#include <vector>

export module forge.chain.transaction.signing;

export import forge.chain.transaction.exceptions;
export import forge.chain.transaction.types;
export import forge.crypto.signer.provider;

export namespace forge::chain::transaction {

[[nodiscard]] boost::asio::awaitable<prepared_transaction>
sign(unsigned_transaction value, std::vector<signing_key> keys, crypto::signer::provider& signer);

[[nodiscard]] prepared_transaction pack(chain::protocol::signed_transaction value, compression_type compression);

} // namespace forge::chain::transaction
