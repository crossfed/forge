module;

#include <boost/asio/awaitable.hpp>
#include <boost/describe.hpp>

#include <memory>
#include <string>
#include <vector>

export module forge.db.rocksdb;

import forge.db.driver;
import forge.rocksdb.store;

export namespace forge::db::rocksdb {

struct config {
   std::string path;
   std::vector<forge::rocksdb::column_family_config> families{"default"};
   forge::rocksdb::write_options write;
   bool create_if_missing = true;
   bool create_missing_column_families = true;
};

class driver : public forge::db::driver {
 public:
   explicit driver(config value);

   boost::asio::awaitable<void> async_flush(bool sync) override;
   void flush(bool sync = true);

 private:
   boost::asio::awaitable<std::unique_ptr<forge::db::session>> open_transaction() override;
   boost::asio::awaitable<std::unique_ptr<forge::db::session>> open_snapshot() override;

   std::shared_ptr<forge::rocksdb::store> store_;
   forge::rocksdb::write_options write_;
};

} // namespace forge::db::rocksdb

export namespace forge::db::rocksdb {

BOOST_DESCRIBE_STRUCT(config, (), (path, families, write, create_if_missing, create_missing_column_families))

} // namespace forge::db::rocksdb
