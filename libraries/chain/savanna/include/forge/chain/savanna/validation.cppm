module;

#include <boost/describe.hpp>
#include <forge/exceptions/macros.hpp>

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

export module forge.chain.savanna.validation;

export import forge.chain.savanna.types;
export import forge.chain.savanna.exceptions;
export import forge.chain.core.merkle;

import forge.raw.exceptions;
import forge.raw.raw;

export namespace forge::chain::savanna {

struct validation_leaf {
   block_num_t num = 0;
   block_slot_t slot = 0;
   block_slot_t parent_slot = 0;
   digest finality_digest;
   digest commitment;
};

class validation_state;
void validate(const validation_state& state);

class validation_state {
 public:
   validation_state() = default;

   [[nodiscard]] bool empty() const noexcept;
   [[nodiscard]] block_num_t first_block_num() const;
   [[nodiscard]] block_num_t current_block_num() const;
   [[nodiscard]] std::size_t retained_size() const noexcept;
   [[nodiscard]] digest root() const;

 private:
   static constexpr auto raw_version = std::uint32_t{1};

   template <typename Stream> friend Stream& operator<<(Stream& stream, const validation_state& value) {
      forge::raw::pack(stream, raw_version);
      forge::raw::pack(stream, value.prefix_);
      forge::raw::pack(stream, value.current_);
      forge::raw::pack(stream, value.first_);
      forge::raw::pack(stream, value.roots_);
      forge::raw::pack(stream, value.leaves_);
      return stream;
   }

   template <typename Stream> friend Stream& operator>>(Stream& stream, validation_state& value) {
      auto version = std::uint32_t{};
      auto decoded = validation_state{};
      forge::raw::unpack(stream, version);
      if (version != raw_version) {
         FORGE_THROW_EXCEPTION(forge::raw::exceptions::codec_error,
                               "unsupported Savanna validation state version",
                               forge::exceptions::ctx("version", version));
      }

      forge::raw::unpack(stream, decoded.prefix_);
      forge::raw::unpack(stream, decoded.current_);
      forge::raw::unpack(stream, decoded.first_);
      forge::raw::unpack(stream, decoded.roots_);
      forge::raw::unpack(stream, decoded.leaves_);
      try {
         validate(decoded);
      } catch (const exceptions::invalid_validation_state&) {
         FORGE_THROW_EXCEPTION(forge::raw::exceptions::codec_error,
                               "Savanna validation state is corrupted");
      }
      value = std::move(decoded);
      return stream;
   }

   forge::chain::core::incremental_merkle_tree prefix_;
   forge::chain::core::incremental_merkle_tree current_;
   block_num_t first_ = 0;
   std::vector<digest> roots_;
   std::vector<validation_leaf> leaves_;

   friend validation_state make_validation(const validation_leaf&);
   friend validation_state append(validation_state, const validation_leaf&);
   friend validation_state advance_finalized(validation_state, block_num_t);
   friend digest root_at(const validation_state&, block_num_t);
   friend void validate(const validation_state&);
};

[[nodiscard]] validation_state make_validation(const validation_leaf& genesis);
[[nodiscard]] validation_state append(validation_state state, const validation_leaf& leaf);
[[nodiscard]] validation_state advance_finalized(validation_state state, block_num_t finalized);
[[nodiscard]] digest root_at(const validation_state& state, block_num_t num);

BOOST_DESCRIBE_STRUCT(validation_leaf, (), (num, slot, parent_slot, finality_digest, commitment))

} // namespace forge::chain::savanna
