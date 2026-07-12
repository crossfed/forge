#pragma once

#include <boost/asio/awaitable.hpp>

#include <memory>
#include <string>
#include <vector>

namespace forge::plugins::log::otlp {

struct attached_logger {
   std::string name;
   forge::logger logger;
};

struct plugin::impl {
   config settings;
   forge::asio::runtime* runtime = nullptr;
   std::shared_ptr<forge::otlp::log_exporter> exporter;
   std::shared_ptr<forge::otlp::log_sink> sink;
   forge::otlp::crash_guard crash_guard;
   std::vector<attached_logger> attached_loggers;
   bool started = false;
   bool stopping = false;

   [[nodiscard]] bool available() const noexcept;
   [[nodiscard]] metrics current_metrics() const;
   boost::asio::awaitable<void> flush();
   void detach_sink() noexcept;
};

} // namespace forge::plugins::log::otlp
