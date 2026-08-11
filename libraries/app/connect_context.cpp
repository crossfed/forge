module;

module forge.app.connect_context;

namespace forge::app {

connect_context::connect_context(forge::api::core::view apis, service_registry& services) noexcept
    : apis_{apis}, services_{&services} {}

} // namespace forge::app
