module;

#include <boost/asio/awaitable.hpp>

export module forge.auth.workload.subject_token_source;

export import forge.auth.workload.exceptions;
export import forge.auth.workload.subject_token;

export namespace forge::auth::workload {

class subject_token_source {
 public:
   virtual ~subject_token_source();

   [[nodiscard]] virtual boost::asio::awaitable<subject_token> async_subject_token() = 0;
};

} // namespace forge::auth::workload
