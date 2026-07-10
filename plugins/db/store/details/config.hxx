#pragma once

namespace forge::plugins::db::store::detail {

[[nodiscard]] config decode_config(const forge::config::core::component_view& view);
void validate_config(const config& value);
[[nodiscard]] forge::db::object::store::options parse_object_options(const object_layer_config& value,
                                                                      const std::string& store_name);
[[nodiscard]] store_options parse_options(const store_config& value);
void validate_options(const store_options& value, const std::string& store_name, bool programmatic);

} // namespace forge::plugins::db::store::detail
