module;

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <ios>
#include <streambuf>
#include <type_traits>
#include <utility>
#include <vector>

export module forge.raw.datastream;

export import forge.raw.stream;

export namespace forge {

template <typename StreamBuffer>
class datastream<StreamBuffer,
                 std::enable_if_t<std::is_base_of_v<std::streambuf, std::remove_reference_t<StreamBuffer>>>> {
 public:
   template <typename... Args> explicit datastream(Args&&... args) : buffer_(std::forward<Args>(args)...) {}

   std::size_t read(char* data, std::size_t size) {
      const auto read_size = buffer_.sgetn(data, static_cast<std::streamsize>(size));
      if (read_size < 0 || static_cast<std::size_t>(read_size) != size) {
         const auto consumed = read_size > 0 ? std::min(size, static_cast<std::size_t>(read_size)) : 0U;
         raw::detail::raise_stream_range("read", consumed, static_cast<std::int64_t>(size - consumed));
      }
      return size;
   }

   std::size_t write(const char* data, std::size_t size) {
      const auto written_size = buffer_.sputn(data, static_cast<std::streamsize>(size));
      if (written_size < 0 || static_cast<std::size_t>(written_size) != size) {
         const auto consumed = written_size > 0 ? std::min(size, static_cast<std::size_t>(written_size)) : 0U;
         raw::detail::raise_stream_range("write", consumed, static_cast<std::int64_t>(size - consumed));
      }
      return size;
   }

   std::size_t tellp() {
      return static_cast<std::size_t>(buffer_.pubseekoff(0, std::ios::cur));
   }

   bool skip(std::size_t size) {
      buffer_.pubseekoff(static_cast<std::streamoff>(size), std::ios::cur);
      return true;
   }

   bool get(char& value) {
      read(&value, 1U);
      return true;
   }

   bool seekp(std::size_t offset) {
      buffer_.pubseekoff(static_cast<std::streamoff>(offset), std::ios::beg);
      return true;
   }

   std::size_t remaining() {
      return static_cast<std::size_t>(buffer_.in_avail());
   }

   StreamBuffer& storage() {
      return buffer_;
   }

   const StreamBuffer& storage() const {
      return buffer_;
   }

 private:
   StreamBuffer buffer_;
};

template <typename Byte, typename Allocator>
class datastream<std::deque<Byte, Allocator>,
                 std::enable_if_t<std::is_same_v<Byte, char> || std::is_same_v<Byte, std::uint8_t>>> {
 public:
   using storage_type = std::deque<Byte, Allocator>;

   explicit datastream(storage_type storage = {}) : storage_(std::move(storage)) {}

   std::size_t read(char* destination, std::size_t size) {
      if (size > remaining()) {
         raw::detail::raise_stream_range("read", storage_.size(), static_cast<std::int64_t>(size - remaining()));
      }
      std::copy_n(storage_.begin() + static_cast<std::ptrdiff_t>(position_), size, destination);
      position_ += size;
      return size;
   }

   std::size_t write(const char* source, std::size_t size) {
      storage_.resize(std::max(position_ + size, storage_.size()));
      std::copy_n(source, size, storage_.begin() + static_cast<std::ptrdiff_t>(position_));
      position_ += size;
      return size;
   }

   bool seekp(std::size_t position) {
      if (position > storage_.size()) {
         return false;
      }
      position_ = position;
      return true;
   }

   std::size_t tellp() const {
      return position_;
   }

   bool skip(std::size_t size) {
      return seekp(position_ + size);
   }

   bool get(char& value) {
      read(&value, 1);
      return true;
   }

   std::size_t remaining() const {
      return storage_.size() - position_;
   }

   storage_type& storage() {
      return storage_;
   }

   const storage_type& storage() const {
      return storage_;
   }

 private:
   storage_type storage_;
   std::size_t position_ = 0;
};

template <typename Stream> class datastream_mirror {
 public:
   explicit datastream_mirror(Stream& stream, std::size_t reserve = 0) : stream_(stream) {
      mirror_.reserve(reserve);
   }

   void skip(std::size_t size) {
      stream_.skip(size);
   }

   bool read(char* destination, std::size_t size) {
      if (!stream_.read(destination, size)) {
         return false;
      }

      const auto previous_size = mirror_.size();
      if (mirror_.capacity() < previous_size + size) {
         mirror_.reserve(std::bit_ceil(previous_size + size));
      }
      mirror_.resize(previous_size + size);
      std::memcpy(mirror_.data() + previous_size, destination, size);
      return true;
   }

   bool get(unsigned char& value) {
      return read(reinterpret_cast<char*>(&value), 1);
   }

   bool get(char& value) {
      return read(&value, 1);
   }

   std::vector<std::uint8_t> extract_mirror() {
      return std::move(mirror_);
   }

 private:
   Stream& stream_;
   std::vector<std::uint8_t> mirror_;
};

} // namespace forge
