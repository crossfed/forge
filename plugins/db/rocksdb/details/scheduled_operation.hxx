#pragma once

namespace forge::plugins::db::rocksdb::detail {

template <typename T> struct scheduled_result {
   std::mutex mutex;
   std::optional<T> value;
   std::exception_ptr error;
};

template <> struct scheduled_result<void> {
   std::mutex mutex;
   std::exception_ptr error;
};

template <typename Fn>
boost::asio::awaitable<std::invoke_result_t<Fn&>> run_scheduled(
   forge::asio::task::scheduler& scheduler,
   std::string name,
   Fn fn) {
   using result_type = std::invoke_result_t<Fn&>;
   auto state = std::make_shared<scheduled_result<result_type>>();
   auto handle = scheduler.submit(
      forge::asio::task::task{
         .priority = forge::asio::task::priority{},
         .name = std::move(name),
         .work =
            [state, fn = std::move(fn)]() mutable {
               try {
                  if constexpr (std::is_void_v<result_type>) {
                     fn();
                  } else {
                     auto value = fn();
                     const auto lock = std::scoped_lock{state->mutex};
                     state->value = std::move(value);
                  }
               } catch (...) {
                  const auto lock = std::scoped_lock{state->mutex};
                  state->error = std::current_exception();
               }
            },
      });

   co_await handle.wait();

   auto lock = std::scoped_lock{state->mutex};
   if (state->error) {
      std::rethrow_exception(state->error);
   }
   if constexpr (std::is_void_v<result_type>) {
      co_return;
   } else {
      co_return std::move(*state->value);
   }
}

} // namespace forge::plugins::db::rocksdb::detail
