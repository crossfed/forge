module;

#include <forge/exceptions/macros.hpp>

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <span>
#include <string>
#include <vector>

module forge.compression.zlib;

import forge.compression.exceptions;

namespace forge::compression {
namespace {

constexpr auto chunk_size = std::size_t{16 * 1024};

std::string zlib_message(const z_stream& stream, int code) {
   if (stream.msg != nullptr) {
      return stream.msg;
   }
   return "zlib error " + std::to_string(code);
}

class zlib_stream {
 public:
   enum class mode {
      deflate,
      inflate,
   };

   explicit zlib_stream(mode selected_mode, int level = Z_DEFAULT_COMPRESSION)
       : selected_mode_{selected_mode} {
      const auto result = selected_mode_ == mode::deflate ? deflateInit(&stream_, level)
                                                          : inflateInit(&stream_);
      if (result != Z_OK) {
         FORGE_THROW_EXCEPTION(exceptions::backend_error, zlib_message(stream_, result));
      }
      initialized_ = true;
   }

   zlib_stream(const zlib_stream&) = delete;
   zlib_stream& operator=(const zlib_stream&) = delete;

   ~zlib_stream() {
      if (!initialized_) {
         return;
      }
      if (selected_mode_ == mode::deflate) {
         deflateEnd(&stream_);
      } else {
         inflateEnd(&stream_);
      }
   }

   z_stream& get() {
      return stream_;
   }

 private:
   mode selected_mode_;
   z_stream stream_{};
   bool initialized_ = false;
};

uInt next_input_size(std::size_t remaining) {
   return static_cast<uInt>(std::min<std::size_t>(remaining, std::numeric_limits<uInt>::max()));
}

} // namespace

std::vector<char> zlib_compress(std::span<const char> input, zlib_level level) {
   auto stream_owner = zlib_stream{zlib_stream::mode::deflate, static_cast<int>(level)};
   auto& stream = stream_owner.get();
   auto out = std::vector<char>{};
   auto buffer = std::array<unsigned char, chunk_size>{};

   const auto* input_ptr = reinterpret_cast<const unsigned char*>(input.data());
   auto remaining = input.size();
   auto result = Z_OK;
   do {
      const auto available = next_input_size(remaining);
      stream.next_in = const_cast<unsigned char*>(input_ptr);
      stream.avail_in = available;
      input_ptr += available;
      remaining -= available;

      const auto flush = remaining == 0 ? Z_FINISH : Z_NO_FLUSH;
      do {
         stream.next_out = buffer.data();
         stream.avail_out = static_cast<uInt>(buffer.size());
         result = deflate(&stream, flush);
         if (result == Z_STREAM_ERROR) {
            FORGE_THROW_EXCEPTION(exceptions::backend_error, zlib_message(stream, result));
         }

         const auto produced = buffer.size() - stream.avail_out;
         out.insert(out.end(),
                    reinterpret_cast<const char*>(buffer.data()),
                    reinterpret_cast<const char*>(buffer.data()) + produced);
      } while (stream.avail_out == 0);
   } while (result != Z_STREAM_END);

   return out;
}

std::vector<char> zlib_decompress(std::span<const char> input, zlib_limits limits) {
   auto stream_owner = zlib_stream{zlib_stream::mode::inflate};
   auto& stream = stream_owner.get();
   auto out = std::vector<char>{};
   auto buffer = std::array<unsigned char, chunk_size>{};

   const auto* input_ptr = reinterpret_cast<const unsigned char*>(input.data());
   auto remaining = input.size();
   auto loaded_all_input = false;

   while (true) {
      if (stream.avail_in == 0 && remaining > 0) {
         const auto available = next_input_size(remaining);
         stream.next_in = const_cast<unsigned char*>(input_ptr);
         stream.avail_in = available;
         input_ptr += available;
         remaining -= available;
      }
      loaded_all_input = remaining == 0;

      const auto at_limit = out.size() >= limits.max_output_size;
      const auto capacity = at_limit ? std::size_t{1}
                                     : std::min(buffer.size(), limits.max_output_size - out.size());
      stream.next_out = buffer.data();
      stream.avail_out = static_cast<uInt>(capacity);

      const auto result = inflate(&stream, Z_NO_FLUSH);
      const auto produced = capacity - stream.avail_out;
      if (produced > 0) {
         if (at_limit || out.size() + produced > limits.max_output_size) {
            FORGE_THROW_EXCEPTION(exceptions::output_limit, "zlib decompressed output exceeds configured limit");
         }
         out.insert(out.end(),
                    reinterpret_cast<const char*>(buffer.data()),
                    reinterpret_cast<const char*>(buffer.data()) + produced);
      }

      if (result == Z_STREAM_END) {
         return out;
      }
      if (result == Z_OK) {
         continue;
      }
      if (result == Z_BUF_ERROR && loaded_all_input) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_input, zlib_message(stream, result));
      }
      if (result != Z_BUF_ERROR) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_input, zlib_message(stream, result));
      }
   }
}

} // namespace forge::compression
