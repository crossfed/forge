module;

#include <boost/asio/awaitable.hpp>

#include <memory>
#include <utility>

export module forge.api.core.stream_writer;

export import forge.api.core.stream_reader;

export namespace forge::api::core {

template <typename T>
class stream_writer;

namespace detail {

class writer_access {
 public:
   template <typename T>
   [[nodiscard]] static stream_writer<T>
   make(std::shared_ptr<stream_endpoint> endpoint) {
      return stream_writer<T>{std::move(endpoint)};
   }

   template <typename T>
   [[nodiscard]] static const std::shared_ptr<stream_endpoint>&
   endpoint(const stream_writer<T>& value) {
      return value.endpoint_;
   }
};

} // namespace detail

template <typename T>
class stream_writer {
 public:
   stream_writer() = default;

   ~stream_writer() {
      close();
   }

   stream_writer(stream_writer&& other) noexcept
       : endpoint_{std::exchange(other.endpoint_, {})} {}

   stream_writer& operator=(stream_writer&& other) noexcept {
      if (this != &other) {
         close();
         endpoint_ = std::exchange(other.endpoint_, {});
      }
      return *this;
   }

   stream_writer(const stream_writer&) = delete;
   stream_writer& operator=(const stream_writer&) = delete;

   [[nodiscard]] explicit operator bool() const noexcept {
      return static_cast<bool>(endpoint_);
   }

   boost::asio::awaitable<void> async_write(T value) {
      if (!endpoint_) {
         throw exceptions::protocol_error{"invalid API stream writer"};
      }
      co_await endpoint_->async_write(forge::raw::pack(value));
   }

   boost::asio::awaitable<void> async_close() {
      close();
      co_return;
   }

 private:
   explicit stream_writer(std::shared_ptr<detail::stream_endpoint> endpoint)
       : endpoint_{std::move(endpoint)} {}

   void close() noexcept {
      if (endpoint_) {
         endpoint_->close();
      }
   }

   std::shared_ptr<detail::stream_endpoint> endpoint_;

   friend class detail::writer_access;
};

template <typename T>
struct stream_writer_traits {
   static constexpr bool value = false;
};

template <typename T>
struct stream_writer_traits<stream_writer<T>> {
   static constexpr bool value = true;
   using item_type = T;
};

} // namespace forge::api::core
