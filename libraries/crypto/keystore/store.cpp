module;

#include <boost/asio/awaitable.hpp>

#include <coroutine>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

module forge.crypto.keystore.store;

#include "details/store_impl.hxx"

namespace forge::crypto::keystore {

store::store(std::unique_ptr<impl> implementation) : impl_(std::move(implementation)) {}

store store::create(std::filesystem::path path, core::secret_string password, store_options options) {
   return store{impl::create(std::move(path), std::move(password), options)};
}

store store::open(std::filesystem::path path, core::secret_string password, store_options options) {
   return store{impl::open(std::move(path), std::move(password), options)};
}

store::~store() = default;
store::store(store&&) noexcept = default;
store& store::operator=(store&&) noexcept = default;

void store::put(signer::key_id id, asymmetric::private_key key, bool replace) {
   impl_->put(std::move(id), std::move(key), replace);
}

void store::erase(const signer::key_id& id) {
   impl_->erase(id);
}

void store::save() {
   impl_->save();
}

const std::filesystem::path& store::path() const noexcept {
   return impl_->path();
}

boost::asio::awaitable<std::vector<signer::key_info>> store::keys() {
   co_return impl_->keys();
}

boost::asio::awaitable<signer::key_info> store::describe(const signer::key_id& id) {
   co_return impl_->describe(id);
}

boost::asio::awaitable<signer::sign_digest_response> store::sign_digest(signer::sign_digest_request request) {
   co_return impl_->sign_digest(request);
}

} // namespace forge::crypto::keystore
