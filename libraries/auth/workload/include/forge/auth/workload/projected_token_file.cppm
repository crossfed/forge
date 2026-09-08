module;

#include <boost/asio/awaitable.hpp>

#include <cstddef>
#include <filesystem>
#include <optional>

export module forge.auth.workload.projected_token_file;

export import forge.asio.compute;
export import forge.auth.workload.exceptions;
export import forge.auth.workload.subject_token_source;

export namespace forge::auth::workload {

struct projected_token_file_options {
   std::filesystem::path path;
   std::size_t max_bytes = 64U * 1024U;
   std::size_t max_rotation_attempts = 3;
};

class projected_token_file final : public subject_token_source {
 public:
   projected_token_file(forge::asio::compute::executor read_executor, projected_token_file_options options);

   [[nodiscard]] boost::asio::awaitable<subject_token> async_subject_token() override;

 private:
   forge::asio::compute::executor read_executor_;
   projected_token_file_options options_;
};

} // namespace forge::auth::workload
