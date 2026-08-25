module;

#include <forge/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>

#include <memory>
#include <string_view>
#include <utility>

module forge.api.core.dispatcher;

namespace forge::api::core {
namespace {

[[nodiscard]] bool reserved_metadata_key(std::string_view key) noexcept {
   return key.starts_with(trusted_metadata_prefix);
}

void apply_remote_metadata_boundary(frame& value,
                                    const metadata& trusted) {
   auto merged = metadata{};
   merged.reserve(value.meta.size() + trusted.size());
   for (auto& entry : value.meta) {
      if (!reserved_metadata_key(entry.key)) {
         merged.push_back(std::move(entry));
      }
   }
   for (const auto& entry : trusted) {
      merged.push_back(entry);
   }
   value.meta = std::move(merged);
}

void validate_request(const frame& value, const dispatch_options& options) {
   if (value.kind != frame_kind::request) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error,
                            "API dispatcher accepts only request frames");
   }
   if (value.id.value == 0) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error,
                            "API call_id zero is reserved for session control");
   }
   if (value.codec != options.codec) {
      FORGE_THROW_EXCEPTION(exceptions::codec_failed,
                            "API frame codec is not accepted",
                            forge::exceptions::ctx("codec",
                                                   value.codec.value));
   }
}

template <typename Plan>
boost::asio::awaitable<frame>
dispatch_selected(Plan plan, dispatch_options options, std::optional<trusted_invocation> trusted, frame value) {
   validate_request(value, options);
   apply_remote_metadata_boundary(value, options.trusted_metadata);
   if (!trusted) {
      co_return co_await plan.dispatch(std::move(value));
   }
   co_return co_await plan.dispatch_contextual(std::move(value), std::move(*trusted));
}

template <typename Plan>
boost::asio::awaitable<frame>
dispatch_stream_selected(Plan plan, dispatch_options options, std::optional<trusted_invocation> trusted, frame value,
                         std::shared_ptr<detail::stream_endpoint> input,
                         std::shared_ptr<detail::stream_endpoint> output) {
   validate_request(value, options);
   apply_remote_metadata_boundary(value, options.trusted_metadata);
   if (!trusted) {
      co_return co_await plan.dispatch_stream(std::move(value), std::move(input), std::move(output));
   }
   co_return co_await plan.dispatch_stream_contextual(
      std::move(value), std::move(input), std::move(output), std::move(*trusted));
}

} // namespace

struct frame_dispatcher::impl {
   binding_plan plan;
   dispatch_options options;
   std::optional<trusted_invocation> trusted;

   impl(binding_plan plan_value, dispatch_options options_value,
        std::optional<trusted_invocation> trusted_value = std::nullopt)
       : plan{std::move(plan_value)}, options{std::move(options_value)},
         trusted{std::move(trusted_value)} {}
};

frame_dispatcher::frame_dispatcher(binding_plan plan, dispatch_options options)
    : impl_{std::make_shared<impl>(std::move(plan), std::move(options))} {}

frame_dispatcher::frame_dispatcher(binding_plan plan, dispatch_options options,
                                   trusted_invocation trusted)
    : impl_{std::make_shared<impl>(std::move(plan), std::move(options),
                                   std::move(trusted))} {}

frame_dispatcher::~frame_dispatcher() = default;
frame_dispatcher::frame_dispatcher(frame_dispatcher&&) noexcept = default;
frame_dispatcher&
frame_dispatcher::operator=(frame_dispatcher&&) noexcept = default;

boost::asio::awaitable<frame> frame_dispatcher::dispatch(frame value) {
   if (!impl_) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error,
                            "invalid API frame dispatcher");
   }
   co_return co_await dispatch_selected(impl_->plan, impl_->options, impl_->trusted, std::move(value));
}

boost::asio::awaitable<frame>
frame_dispatcher::dispatch(frame value, pinned_binding_plan selected) {
   if (!impl_) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error,
                            "invalid API frame dispatcher");
   }
   co_return co_await dispatch_selected(
      std::move(selected), impl_->options, impl_->trusted, std::move(value));
}

boost::asio::awaitable<frame>
frame_dispatcher::dispatch_stream(
   frame value, std::shared_ptr<detail::stream_endpoint> input,
   std::shared_ptr<detail::stream_endpoint> output) {
   if (!impl_) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error,
                            "invalid API frame dispatcher");
   }
   co_return co_await dispatch_stream_selected(
      impl_->plan, impl_->options, impl_->trusted, std::move(value), std::move(input), std::move(output));
}

boost::asio::awaitable<frame>
frame_dispatcher::dispatch_stream(
   frame value, std::shared_ptr<detail::stream_endpoint> input,
   std::shared_ptr<detail::stream_endpoint> output, pinned_binding_plan selected) {
   if (!impl_) {
      FORGE_THROW_EXCEPTION(exceptions::protocol_error,
                            "invalid API frame dispatcher");
   }
   co_return co_await dispatch_stream_selected(
      std::move(selected), impl_->options, impl_->trusted, std::move(value), std::move(input), std::move(output));
}

const dispatch_options& frame_dispatcher::options() const noexcept {
   return impl_->options;
}

} // namespace forge::api::core
