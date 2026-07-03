module;

#include <boost/asio/awaitable.hpp>

#include <memory>

export module forge.objectdb.driver;

import forge.objectdb.session;

export namespace forge::objectdb {

class driver {
 public:
   virtual ~driver() = default;

   virtual boost::asio::awaitable<std::unique_ptr<session>> begin_transaction() = 0;
   virtual boost::asio::awaitable<std::unique_ptr<session>> begin_read() = 0;
   virtual boost::asio::awaitable<void> flush(bool sync) = 0;
};

} // namespace forge::objectdb
