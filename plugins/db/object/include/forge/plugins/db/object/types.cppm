module;

#include <boost/describe.hpp>

#include <cstdint>
#include <string>
#include <vector>

export module forge.plugins.db.object.types;

import forge.schema.object;
import forge.schema.value_kind;

export namespace forge::plugins::db::object {

struct store_config {
   std::string name;
   std::string driver = "rocksdb";
   std::string path;
   std::string family = "objectdb";
   std::string write_policy = "single-writer";
   bool create_if_missing = true;
   bool create_missing_column_families = true;
};

struct config {
   std::vector<store_config> stores;
};

struct store_status {
   std::string name;
   std::string driver;
   std::string path;
   std::string family;
   bool started = false;
};

struct status {
   std::vector<store_status> stores;
};

BOOST_DESCRIBE_STRUCT(store_config,
                      (),
                      (name,
                       driver,
                       path,
                       family,
                       write_policy,
                       create_if_missing,
                       create_missing_column_families))
BOOST_DESCRIBE_STRUCT(config, (), (stores))
BOOST_DESCRIBE_STRUCT(store_status, (), (name, driver, path, family, started))
BOOST_DESCRIBE_STRUCT(status, (), (stores))

} // namespace forge::plugins::db::object

export template <> struct forge::schema::rules<forge::plugins::db::object::store_config> {
   [[nodiscard]] static forge::schema::object_schema<forge::plugins::db::object::store_config> define() {
      auto schema = forge::schema::object<forge::plugins::db::object::store_config>();
      schema.field<&forge::plugins::db::object::store_config::name>("name").required().non_empty();
      schema.field<&forge::plugins::db::object::store_config::driver>("driver")
         .default_value("rocksdb")
         .non_empty();
      schema.field<&forge::plugins::db::object::store_config::path>("path").required().non_empty();
      schema.field<&forge::plugins::db::object::store_config::family>("family")
         .default_value("objectdb")
         .non_empty();
      schema.field<&forge::plugins::db::object::store_config::write_policy>("write-policy")
         .default_value("single-writer")
         .non_empty();
      schema.field<&forge::plugins::db::object::store_config::create_if_missing>("create-if-missing")
         .default_value(true);
      schema.field<&forge::plugins::db::object::store_config::create_missing_column_families>(
         "create-missing-column-families")
         .default_value(true);
      return schema;
   }
};

export template <> struct forge::schema::rules<forge::plugins::db::object::config> {
   [[nodiscard]] static forge::schema::object_schema<forge::plugins::db::object::config> define() {
      auto schema = forge::schema::object<forge::plugins::db::object::config>();
      schema.field<&forge::plugins::db::object::config::stores>("stores")
         .items<forge::plugins::db::object::store_config>()
         .unique_by<&forge::plugins::db::object::store_config::name>();
      return schema;
   }
};
