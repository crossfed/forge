#pragma once

namespace forge::plugins::crypto::signer {

[[nodiscard]] config decode_config(const forge::config::core::component_view& view);
void apply_config(plugin::impl& state, forge::config::core::component_view view);

} // namespace forge::plugins::crypto::signer
