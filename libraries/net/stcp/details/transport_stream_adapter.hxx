#pragma once

namespace forge::net::stcp::detail {

class transport_stream_adapter {
 private:
   using completion = boost::compat::move_only_function<void(boost::system::error_code, std::size_t)>;

   class state final : public std::enable_shared_from_this<state> {
    public:
      state(boost::asio::any_io_executor executor, forge::net::transport::stream stream);
      ~state();

      [[nodiscard]] boost::asio::any_io_executor get_executor() const noexcept;
      [[nodiscard]] bool valid() const noexcept;
      void request_cancel() noexcept;
      boost::asio::awaitable<void> async_close();
      void start_read(std::vector<boost::asio::mutable_buffer> destination, completion handler);
      void start_write(std::vector<boost::asio::const_buffer> source, completion handler);

      template <typename handler_type> [[nodiscard]] completion make_completion(handler_type&& handler) {
         auto executor = boost::asio::get_associated_executor(handler, executor_);
         auto cancellation = boost::asio::get_associated_cancellation_slot(handler);
         auto terminal = std::make_shared<std::atomic_uint8_t>(0U);
         if (cancellation.is_connected()) {
            cancellation.assign([weak = weak_from_this(), terminal](boost::asio::cancellation_type_t type) noexcept {
               auto expected = std::uint8_t{0};
               if (type != boost::asio::cancellation_type::none &&
                   terminal->compare_exchange_strong(expected, std::uint8_t{1}, std::memory_order_acq_rel,
                                                     std::memory_order_acquire)) {
                  if (const auto self = weak.lock()) {
                     self->request_cancel();
                  }
               }
            });
         }
         return [handler = std::forward<handler_type>(handler), executor, terminal](
                    boost::system::error_code error, std::size_t size) mutable {
            terminal->store(std::uint8_t{2}, std::memory_order_release);
            boost::asio::dispatch(executor, [handler = std::move(handler), error, size]() mutable {
               std::move(handler)(error, size);
            });
         };
      }

    private:
      boost::asio::awaitable<std::size_t> async_read_some(std::vector<boost::asio::mutable_buffer> destination);
      boost::asio::awaitable<std::size_t> async_write_some(std::vector<boost::asio::const_buffer> source);

      boost::asio::any_io_executor executor_;
      forge::net::transport::stream stream_;
      std::vector<std::uint8_t> pending_read_;
      std::atomic_bool cancel_requested_ = false;
   };

 public:
   using executor_type = boost::asio::any_io_executor;

   transport_stream_adapter();
   transport_stream_adapter(boost::asio::any_io_executor executor, forge::net::transport::stream stream);
   ~transport_stream_adapter();

   transport_stream_adapter(transport_stream_adapter&&) noexcept;
   transport_stream_adapter& operator=(transport_stream_adapter&&) noexcept;

   transport_stream_adapter(const transport_stream_adapter&) = delete;
   transport_stream_adapter& operator=(const transport_stream_adapter&) = delete;

   [[nodiscard]] executor_type get_executor() const noexcept;
   [[nodiscard]] bool valid() const noexcept;
   using lowest_layer_type = transport_stream_adapter;

   [[nodiscard]] transport_stream_adapter& lowest_layer() noexcept;
   [[nodiscard]] const transport_stream_adapter& lowest_layer() const noexcept;

   void request_cancel() noexcept;
   boost::asio::awaitable<void> async_close();

   template <typename mutable_buffer_sequence, typename completion_token>
   auto async_read_some(const mutable_buffer_sequence& buffers, completion_token&& token) {
      auto destination = std::vector<boost::asio::mutable_buffer>{};
      for (auto iterator = boost::asio::buffer_sequence_begin(buffers);
           iterator != boost::asio::buffer_sequence_end(buffers); ++iterator) {
         destination.push_back(*iterator);
      }
      auto state = state_;
      return boost::asio::async_initiate<completion_token, void(boost::system::error_code, std::size_t)>(
          [state = std::move(state), destination = std::move(destination)](auto handler) mutable {
             state->start_read(std::move(destination), state->make_completion(std::move(handler)));
          },
          token);
   }

   template <typename const_buffer_sequence, typename completion_token>
   auto async_write_some(const const_buffer_sequence& buffers, completion_token&& token) {
      auto source = std::vector<boost::asio::const_buffer>{};
      for (auto iterator = boost::asio::buffer_sequence_begin(buffers);
           iterator != boost::asio::buffer_sequence_end(buffers); ++iterator) {
         source.push_back(*iterator);
      }
      auto state = state_;
      return boost::asio::async_initiate<completion_token, void(boost::system::error_code, std::size_t)>(
          [state = std::move(state), source = std::move(source)](auto handler) mutable {
             state->start_write(std::move(source), state->make_completion(std::move(handler)));
          },
          token);
   }

 private:
   std::shared_ptr<state> state_;
};

} // namespace forge::net::stcp::detail
