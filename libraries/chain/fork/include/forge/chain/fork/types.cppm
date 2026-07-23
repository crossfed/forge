module;

#include <boost/describe.hpp>

#include <cstdint>

export module forge.chain.fork.types;

export namespace forge::chain::fork {

enum class insert_status : std::uint8_t {
   inserted,
   duplicate,
   unlinked,
};

[[nodiscard]] bool inserted(insert_status value) noexcept;

BOOST_DESCRIBE_ENUM(insert_status, inserted, duplicate, unlinked)

} // namespace forge::chain::fork
