#pragma once

namespace forge::plugins::db::store::detail {

[[nodiscard]] config decode_config(const forge::config::component_view& view);
void validate_config(const config& value);
[[nodiscard]] forge::db::object::store::options parse_object_options(const object_layer_config& value,
                                                                      const std::string& store_name);
[[nodiscard]] store_options parse_options(const store_config& value);

} // namespace forge::plugins::db::store::detail
