#pragma once

namespace forge::plugins::db::object::detail {

[[nodiscard]] config decode_config(const forge::config::component_view& view);
void validate_config(const config& value);
[[nodiscard]] forge::db::object::store::options parse_options(const store_config& value);

} // namespace forge::plugins::db::object::detail
