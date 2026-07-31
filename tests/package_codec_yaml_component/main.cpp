#include <string>

import forge.codec.yaml;
import forge.schema.object;
import forge.variant.value;
import package.codec.yaml.schema_types;

template <> struct forge::schema::rules<package_codec_yaml::nested_limits> {
   [[nodiscard]] static forge::schema::object_schema<package_codec_yaml::nested_limits> define() {
      auto schema = forge::schema::object<package_codec_yaml::nested_limits>();
      static_cast<void>(schema.field<&package_codec_yaml::nested_limits::deadline_ms>("api.deadline-ms"));
      return schema;
   }
};

int main() {
   const auto input = package_codec_yaml::nested_config{.limits = {.deadline_ms = 2500}};
   const auto written = forge::codec::yaml::write(input);
   if (!written.ok() || written.text.find("deadline-ms:") == std::string::npos ||
       written.text.find("deadline_ms:") != std::string::npos) {
      return 1;
   }

   const auto decoded = forge::codec::yaml::read<package_codec_yaml::nested_config>(written.text);
   return decoded.ok() && decoded.value.limits.deadline_ms == input.limits.deadline_ms ? 0 : 1;
}
