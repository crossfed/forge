#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace forge::crypto::symmetric::xsalsa20 {

struct stream::impl {
   impl(core::secret_bytes key, nonce nonce) noexcept;
   ~impl();

   impl(const impl&) = delete;
   impl& operator=(const impl&) = delete;

   void transform(std::span<std::uint8_t> bytes);

 private:
   void require_capacity(std::size_t size) const;
   void load_block();

   core::secret_bytes key_;
   nonce nonce_;
   std::array<std::uint8_t, block_size> keystream_{};
   std::uint64_t next_block_counter_ = 0;
   std::size_t block_offset_ = block_size;
   bool exhausted_ = false;
};

} // namespace forge::crypto::symmetric::xsalsa20
