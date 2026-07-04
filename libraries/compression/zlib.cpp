module;

#include <forge/exceptions/macros.hpp>

#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/filter/zlib.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/operations.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <ios>
#include <limits>
#include <span>
#include <string>
#include <vector>

module forge.compression.zlib;

import forge.compression.exceptions;

namespace forge::compression {
namespace {

constexpr auto chunk_size = std::size_t{16 * 1024};

int to_boost_level(zlib_level level) {
   return static_cast<int>(level);
}

std::streamsize to_stream_size(std::size_t value) {
   const auto max = static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max());
   return static_cast<std::streamsize>(std::min(value, max));
}

boost::iostreams::zlib_params make_params(zlib_level level) {
   auto params = boost::iostreams::zlib_params{};
   params.level = to_boost_level(level);
   return params;
}

} // namespace

std::vector<char> zlib_compress(std::span<const char> input, zlib_level level) {
   try {
      auto out = std::vector<char>{};
      auto stream = boost::iostreams::filtering_ostream{};
      stream.push(boost::iostreams::zlib_compressor{make_params(level)});
      stream.push(boost::iostreams::back_insert_device(out));

      auto* data = input.data();
      auto remaining = input.size();
      while (remaining > 0) {
         const auto size = to_stream_size(remaining);
         boost::iostreams::write(stream, data, size);
         data += size;
         remaining -= static_cast<std::size_t>(size);
      }

      boost::iostreams::close(stream);
      return out;
   } catch (const boost::iostreams::zlib_error& error) {
      FORGE_THROW_EXCEPTION(exceptions::backend_error, error.what());
   } catch (const std::ios_base::failure& error) {
      FORGE_THROW_EXCEPTION(exceptions::backend_error, error.what());
   }
}

std::vector<char> zlib_decompress(std::span<const char> input, zlib_limits limits) {
   try {
      auto decompressor = boost::iostreams::zlib_decompressor{};
      auto out = std::vector<char>{};
      auto buffer = std::array<char, chunk_size>{};
      auto* input_next = input.data();
      const auto* input_end = input.data() + input.size();

      while (true) {
         const auto at_limit = out.size() >= limits.max_output_size;
         const auto capacity = at_limit ? std::size_t{1}
                                        : std::min(buffer.size(), limits.max_output_size - out.size());

         auto* output_next = buffer.data();
         auto* output_end = buffer.data() + capacity;
         const auto* before_input = input_next;
         const auto keep_going = decompressor.filter().filter(input_next,
                                                               input_end,
                                                               output_next,
                                                               output_end,
                                                               false);
         const auto produced = static_cast<std::size_t>(output_next - buffer.data());
         if (produced > 0) {
            if (at_limit || out.size() + produced > limits.max_output_size) {
               FORGE_THROW_EXCEPTION(exceptions::output_limit, "zlib decompressed output exceeds configured limit");
            }
            out.insert(out.end(), buffer.data(), buffer.data() + produced);
         }

         if (!keep_going) {
            if (input_next != input_end) {
               FORGE_THROW_EXCEPTION(exceptions::invalid_input, "zlib stream has trailing input");
            }
            return out;
         }

         if (produced == 0 && input_next == before_input) {
            FORGE_THROW_EXCEPTION(exceptions::invalid_input, "zlib stream ended before a complete payload");
         }
      }
   } catch (const exceptions::invalid_input&) {
      throw;
   } catch (const exceptions::output_limit&) {
      throw;
   } catch (const boost::iostreams::zlib_error& error) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_input, error.what());
   } catch (const std::ios_base::failure& error) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_input, error.what());
   }
}

} // namespace forge::compression
