#pragma once

namespace forge::plugins::db::objectdb::detail {

[[nodiscard]] config decode_config(const forge::config::core::component_view& view);
void validate_config(const config& value);
[[nodiscard]] forge::objectdb::store::options parse_options(const store_config& value);

} // namespace forge::plugins::db::objectdb::detail
