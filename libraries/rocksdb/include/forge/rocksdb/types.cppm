module;

#include <boost/describe.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module forge.rocksdb.types;

import forge.schema.object;

export namespace forge::rocksdb {

enum class status_code {
   ok,
   not_found,
   invalid_argument,
   corruption,
   io_error,
   timed_out,
   busy,
   unknown,
};

enum class compression_type {
   none,
   snappy,
   zlib,
   bzip2,
   lz4,
   lz4hc,
   xpress,
   zstd,
};

struct blob_options {
   bool enable_blob_files = false;
   std::uint64_t min_blob_size = 0;
   std::uint64_t blob_file_size = 0;
   compression_type blob_compression_type = compression_type::none;
   bool enable_blob_garbage_collection = false;
   double blob_garbage_collection_age_cutoff = 0.25;
};

struct column_family_config {
   column_family_config() = default;

   column_family_config(std::string value) : name{std::move(value)} {}
   column_family_config(const char* value) : name{value == nullptr ? std::string{} : std::string{value}} {}

   std::string name = "default";
   blob_options blobs;
};

struct config {
   std::string path;
   std::vector<column_family_config> column_families;
   bool create_if_missing = true;
   bool create_missing_column_families = true;
};

struct read_options {
   bool verify_checksums = false;
   bool fill_cache = true;
};

struct write_options {
   bool sync = false;
   bool disable_wal = false;
};

class family final {
 public:
   family() = default;

   explicit family(std::string value) : name{std::move(value)} {
      if (name.empty()) {
         throw std::invalid_argument{"RocksDB column family name must not be empty"};
      }
   }

   std::string name = "default";
};

enum class operation_kind {
   put,
   erase,
};

struct entry {
   std::vector<std::byte> key;
   std::vector<std::byte> value;
};

struct scan_request {
   std::vector<std::byte> prefix;
   std::vector<std::byte> cursor;
   std::uint64_t limit = 0;
   read_options options;
   std::vector<std::byte> lower_bound;
};

struct scan_result {
   std::vector<entry> entries;
   std::vector<std::byte> next_cursor;
};

struct operation {
   operation_kind kind = operation_kind::put;
   family column_family;
   std::vector<std::byte> key;
   std::vector<std::byte> value;

   [[nodiscard]] static operation put(family column_family, std::vector<std::byte> key, std::vector<std::byte> value) {
      return operation{
         .kind = operation_kind::put,
         .column_family = std::move(column_family),
         .key = std::move(key),
         .value = std::move(value),
      };
   }

   [[nodiscard]] static operation erase(family column_family, std::vector<std::byte> key) {
      return operation{
         .kind = operation_kind::erase,
         .column_family = std::move(column_family),
         .key = std::move(key),
      };
   }
};

[[nodiscard]] std::vector<std::byte> make_key(std::string_view text);
[[nodiscard]] std::vector<std::byte> make_u64_key(std::uint64_t value);
void append_u8(std::vector<std::byte>& key, std::uint8_t value);
void append_u32_be(std::vector<std::byte>& key, std::uint32_t value);
void append_u64_be(std::vector<std::byte>& key, std::uint64_t value);
[[nodiscard]] std::uint64_t read_u64_be(std::span<const std::byte> bytes);
[[nodiscard]] std::vector<std::byte> to_bytes(std::string_view text);
[[nodiscard]] std::string to_string(std::span<const std::byte> bytes);

} // namespace forge::rocksdb

export namespace forge::rocksdb {

BOOST_DESCRIBE_STRUCT(config, (), (path, column_families, create_if_missing, create_missing_column_families))

BOOST_DESCRIBE_ENUM(compression_type, none, snappy, zlib, bzip2, lz4, lz4hc, xpress, zstd)
BOOST_DESCRIBE_STRUCT(blob_options,
                      (),
                      (enable_blob_files,
                       min_blob_size,
                       blob_file_size,
                       blob_compression_type,
                       enable_blob_garbage_collection,
                       blob_garbage_collection_age_cutoff))
BOOST_DESCRIBE_STRUCT(column_family_config, (), (name, blobs))
BOOST_DESCRIBE_STRUCT(read_options, (), (verify_checksums, fill_cache))
BOOST_DESCRIBE_STRUCT(write_options, (), (sync, disable_wal))
BOOST_DESCRIBE_STRUCT(entry, (), (key, value))
BOOST_DESCRIBE_STRUCT(scan_request, (), (prefix, cursor, limit, options, lower_bound))
BOOST_DESCRIBE_STRUCT(scan_result, (), (entries, next_cursor))

} // namespace forge::rocksdb

namespace rocksdb_schema = ::forge::rocksdb;

export template <> struct forge::schema::rules<rocksdb_schema::blob_options> {
   [[nodiscard]] static forge::schema::object_schema<rocksdb_schema::blob_options> define() {
      auto schema = forge::schema::object<rocksdb_schema::blob_options>();
      schema.field<&rocksdb_schema::blob_options::enable_blob_files>("enable-blob-files")
         .default_value(false)
         .description("Store large values in RocksDB blob files for this column family");
      schema.field<&rocksdb_schema::blob_options::min_blob_size>("min-blob-size")
         .default_value(std::uint64_t{0})
         .description("Minimum value size that RocksDB may place into blob files");
      schema.field<&rocksdb_schema::blob_options::blob_file_size>("blob-file-size")
         .default_value(std::uint64_t{0})
         .description("Target RocksDB blob file size for this column family");
      schema.field<&rocksdb_schema::blob_options::blob_compression_type>("blob-compression-type")
         .default_value(rocksdb_schema::compression_type::none)
         .description("Compression used for RocksDB blob files");
      schema.field<&rocksdb_schema::blob_options::enable_blob_garbage_collection>("enable-blob-garbage-collection")
         .default_value(false)
         .description("Enable RocksDB blob-file garbage collection for this column family");
      schema.field<&rocksdb_schema::blob_options::blob_garbage_collection_age_cutoff>(
         "blob-garbage-collection-age-cutoff")
         .default_value(0.25)
         .range(0.0, 1.0)
         .description("RocksDB blob garbage collection age cutoff");
      return schema;
   }
};

export template <> struct forge::schema::rules<rocksdb_schema::column_family_config> {
   [[nodiscard]] static forge::schema::object_schema<rocksdb_schema::column_family_config> define() {
      auto schema = forge::schema::object<rocksdb_schema::column_family_config>();
      schema.field<&rocksdb_schema::column_family_config::name>("name")
         .required()
         .non_empty()
         .description("RocksDB column family name");
      schema.field<&rocksdb_schema::column_family_config::blobs>("blobs")
         .default_value(rocksdb_schema::blob_options{})
         .description("Optional RocksDB BlobDB settings for this column family");
      return schema;
   }
};

export template <> struct forge::schema::rules<rocksdb_schema::config> {
   [[nodiscard]] static forge::schema::object_schema<rocksdb_schema::config> define() {
      auto schema = forge::schema::object<rocksdb_schema::config>();
      schema.field<&rocksdb_schema::config::path>("path")
         .required()
         .non_empty()
         .description("RocksDB TransactionDB path; required when the rocksdb backend is enabled");
      schema.field<&rocksdb_schema::config::column_families>("column-families")
         .items<rocksdb_schema::column_family_config>()
         .default_value(std::vector<rocksdb_schema::column_family_config>{})
         .description("RocksDB column families to open in addition to default");
      schema.field<&rocksdb_schema::config::create_if_missing>("create-if-missing")
         .default_value(true)
         .description("Create the RocksDB database if it does not exist");
      schema.field<&rocksdb_schema::config::create_missing_column_families>(
         "create-missing-column-families")
         .default_value(true)
         .description("Create configured RocksDB column families if they do not exist");
      return schema;
   }
};
