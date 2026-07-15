module;

#include "details/thread_name.hxx"

#include <boost/asio.hpp>
#include <boost/asio/io_context.hpp>

#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

module forge.asio.runtime;

namespace forge::asio {
struct runtime::impl {
   explicit impl(runtime_options options_value)
      : options(std::move(options_value)), io_context(static_cast<int>(options.worker_threads)),
        work_guard(boost::asio::make_work_guard(io_context)) {
      if (options.worker_threads == 0) {
         throw std::invalid_argument{"asio runtime requires at least one worker thread"};
      }

      workers.reserve(options.worker_threads);
      for (std::size_t index = 0; index < options.worker_threads; ++index) {
         workers.emplace_back([this] {
            detail::set_current_thread_name(options.thread_name);
            io_context.run();
         });
      }
   }

   ~impl() {
      stop();
   }

   void stop() {
      if (stopped) {
         return;
      }
      stopped = true;
      work_guard.reset();
      io_context.stop();
      for (auto& worker : workers) {
         if (worker.joinable()) {
            worker.join();
         }
      }
   }

   runtime_options options;
   boost::asio::io_context io_context;
   decltype(boost::asio::make_work_guard(std::declval<boost::asio::io_context&>())) work_guard;
   std::vector<std::thread> workers;
   bool stopped = false;
};

runtime::runtime(runtime_options options) : impl_(std::make_unique<impl>(std::move(options))) {}

runtime::~runtime() = default;

boost::asio::io_context& runtime::context() {
   return impl_->io_context;
}

const boost::asio::io_context& runtime::context() const {
   return impl_->io_context;
}

void runtime::stop() {
   impl_->stop();
}

} // namespace forge::asio
