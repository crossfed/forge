module;

#include <boost/asio/awaitable.hpp>
#include <forge/exceptions/macros.hpp>

#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <utility>

module forge.plugins.log.otlp.plugin;

import forge.api.core.registry;
import forge.app.plugin;
import forge.app.plugin_context;
import forge.config.core.component;
import forge.config.core.decode;
import forge.log.log_message;
import forge.log.logger;
import forge.otlp.crash;
import forge.otlp.log_exporter;
import forge.otlp.log_sink;
import forge.plugins.log.otlp.api;
import forge.plugins.log.otlp.exceptions;
import forge.plugins.log.otlp.types;

#include "details/config.hxx"
#include "details/plugin_impl.hxx"
#include "details/api_impl.hxx"

namespace forge::plugins::log::otlp {

plugin::plugin() : impl_{std::make_shared<impl>()} {}

plugin::~plugin() = default;

forge::app::plugin_id plugin::id() const {
   return forge::app::plugin_id{.value = "forge.plugins.log.otlp"};
}

std::string plugin::version() const {
   return "1.0.0";
}

std::optional<forge::config::core::component_descriptor> plugin::describe_config() const {
   return forge::config::core::describe_component<config>("plugins.log.otlp");
}

boost::asio::awaitable<void> plugin::configure(forge::config::core::component_view view) {
   impl_->settings = decode_config(view);
   co_return;
}

boost::asio::awaitable<void> plugin::provide(forge::api::core::provider& provider) {
   provider.install<api>(std::make_shared<api_impl>(impl_));
   co_return;
}

boost::asio::awaitable<void> plugin::initialize(forge::app::plugin_context& context) {
   impl_->runtime = &context.scheduler().runtime_context();
   impl_->stopping = false;
   co_return;
}

boost::asio::awaitable<void> plugin::startup() {
   if (impl_->started) {
      co_return;
   }
   impl_->started = true;
   impl_->stopping = false;
   if (!impl_->settings.enabled) {
      co_return;
   }
   if (impl_->runtime == nullptr) {
      FORGE_THROW_EXCEPTION(exceptions::startup_failed, "OTLP logs plugin was not initialized with a runtime");
   }

   try {
      impl_->exporter = std::make_shared<forge::otlp::log_exporter>(
         *impl_->runtime, make_exporter_options(impl_->settings));
      impl_->sink = std::make_shared<forge::otlp::log_sink>(impl_->exporter);
      const auto attach_route = [this](const logger_route& route) {
         auto logger = forge::logger::get(route.name);
         logger.set_name(route.name);
         logger.set_enabled(route.enabled);
         logger.set_log_level(parse_log_level(route.level));
         if (route.export_logs) {
            logger.add_sink(impl_->sink);
            impl_->attached_loggers.push_back(attached_logger{.name = route.name, .logger = logger});
         }
         forge::logger::update(route.name, logger);
      };
      for (const auto& route : impl_->settings.loggers) {
         attach_route(route);
      }
      if (impl_->settings.crash_spool.enabled) {
         const auto crash_options = make_crash_spool_options(impl_->settings);
         impl_->crash_guard = forge::otlp::install_crash_capture(crash_options);
         if (impl_->settings.crash_spool.resend_on_startup) {
            co_await forge::otlp::async_resend_crashes(*impl_->exporter, crash_options);
         }
      }
   } catch (const exceptions::startup_failed&) {
      throw;
   } catch (const std::exception& error) {
      impl_->detach_sink();
      impl_->sink.reset();
      impl_->exporter.reset();
      impl_->started = false;
      FORGE_THROW_EXCEPTION(exceptions::startup_failed, "failed to start OTLP logs exporter",
                            forge::exceptions::ctx("error", error.what()));
   }
}

void plugin::request_stop() noexcept {
   impl_->stopping = true;
}

boost::asio::awaitable<void> plugin::shutdown() {
   impl_->stopping = true;
   impl_->crash_guard = forge::otlp::crash_guard{};
   auto exporter = std::move(impl_->exporter);
   if (exporter) {
      try {
         co_await exporter->async_shutdown();
      } catch (...) {
         impl_->detach_sink();
         impl_->sink.reset();
         impl_->started = false;
         throw;
      }
   }
   impl_->detach_sink();
   impl_->sink.reset();
   impl_->started = false;
   impl_->runtime = nullptr;
}

forge::app::plugin_descriptor descriptor() {
   return forge::app::plugin_descriptor{
      .id = forge::app::plugin_id{.value = "forge.plugins.log.otlp"},
      .factory = [] {
         return std::make_unique<plugin>();
      },
   };
}

} // namespace forge::plugins::log::otlp
