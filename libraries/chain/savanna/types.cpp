module forge.chain.savanna.types;

namespace forge::chain::savanna {

bool block_ref::empty() const noexcept {
   return id.empty();
}

} // namespace forge::chain::savanna
