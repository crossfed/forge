module;

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

export module forge.compression.zlib;

export import forge.compression.exceptions;

export namespace forge::compression {

using bytes = std::vector<std::uint8_t>;

enum class zlib_level : int {
   no_compression = 0,
   best_speed = 1,
   default_compression = -1,
   best_compression = 9,
};

struct zlib_limits {
   std::size_t max_output_size = 1024 * 1024;
};

bytes zlib_compress(std::span<const std::uint8_t> input, zlib_level level = zlib_level::default_compression);
bytes zlib_decompress(std::span<const std::uint8_t> input, zlib_limits limits = {});

} // namespace forge::compression
