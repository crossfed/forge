#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "counter_state.hxx"

namespace forge::crypto::symmetric::xsalsa20 {

struct stream::impl {
   impl(core::secret_bytes key, nonce nonce) noexcept;
   ~impl();

   impl(const impl&) = delete;
   impl& operator=(const impl&) = delete;

   void transform(std::span<std::uint8_t> bytes);

 private:
   void load_block();

   core::secret_bytes key_;
   nonce nonce_;
   std::array<std::uint8_t, block_size> keystream_{};
   detail::counter_state position_;
};

} // namespace forge::crypto::symmetric::xsalsa20
