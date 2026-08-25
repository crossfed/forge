module;

#include <boost/asio/awaitable.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

export module package.chain_api_component.write_fixture;

export import package.chain_api_component.test_support;

import forge.chain.api.admin;
import forge.chain.api.submission;
import forge.chain.api.transaction;

export namespace package_chain_api_component {

namespace chain_api = forge::chain::api;
namespace protocol = forge::chain::protocol;

[[nodiscard]] protocol::transaction_read_only_response
make_transaction_response(const protocol::info_response& source);
[[nodiscard]] protocol::producer_status_response make_admin_response();

class transaction_implementation final : public chain_api::transaction {
 public:
   explicit transaction_implementation(protocol::transaction_read_only_response response);

   boost::asio::awaitable<protocol::transaction_status_response>
   get_status(protocol::transaction_status_request request) override;
   boost::asio::awaitable<protocol::transaction_status_response>
   await_transaction(protocol::transaction_await_request request) override;
   boost::asio::awaitable<std::vector<protocol::public_key>>
   get_required_keys(protocol::transaction_required_keys_request request) override;
   boost::asio::awaitable<protocol::transaction_read_only_response>
   compute_transaction(protocol::transaction_read_only_request request) override;
   boost::asio::awaitable<protocol::transaction_read_only_response>
   send_read_only_transaction(protocol::transaction_read_only_request request) override;

   std::atomic<std::uint32_t> calls{0};
   std::atomic<protocol::audit_mode> last_audit{protocol::audit_mode::none};
   std::atomic<std::uint32_t> await_started{0};
   std::atomic<std::uint32_t> await_deadlines{0};
   std::atomic<std::uint32_t> await_cancellations{0};

 private:
   protocol::transaction_read_only_response response_;
};

class submission_implementation final : public chain_api::submission {
 public:
   boost::asio::awaitable<protocol::transaction_submit_response>
   submit(protocol::transaction_submit_request request) override;
   boost::asio::awaitable<std::vector<protocol::transaction_submit_response>>
   submit_batch(protocol::transaction_submit_batch_request request) override;

   std::atomic<std::uint32_t> calls{0};
   std::atomic<std::uint64_t> last_submit_timeout_ms{0};
   std::atomic<std::uint64_t> last_batch_timeout_ms{0};
};

class admin_implementation final : public chain_api::admin {
 public:
   explicit admin_implementation(protocol::producer_status_response response);

   boost::asio::awaitable<protocol::push_block_response> push_block(protocol::signed_block value) override;
   boost::asio::awaitable<protocol::snapshot_response> create_snapshot(std::string path) override;
   boost::asio::awaitable<protocol::prune_response> prune(protocol::prune_request request) override;
   boost::asio::awaitable<protocol::producer_status_response> producer_status(protocol::admin_query request) override;
   boost::asio::awaitable<protocol::supported_protocol_features_response>
   supported_protocol_features(protocol::supported_protocol_features_request request) override;
   boost::asio::awaitable<protocol::ram_corrections_response>
   account_ram_corrections(protocol::ram_corrections_request request) override;
   boost::asio::awaitable<protocol::unapplied_transactions_response>
   unapplied_transactions(protocol::unapplied_transactions_request request) override;
   boost::asio::awaitable<protocol::snapshot_requests_response> snapshot_requests(protocol::admin_query request) override;
   boost::asio::awaitable<bool> configure_pause(protocol::producer_pause_request request) override;
   boost::asio::awaitable<bool> update_runtime_options(protocol::producer_runtime_options value) override;
   boost::asio::awaitable<bool> update_greylist(protocol::greylist_update_request request) override;
   boost::asio::awaitable<bool> set_access_policy(protocol::producer_access_policy value) override;
   boost::asio::awaitable<protocol::snapshot_schedule>
   schedule_snapshot(protocol::snapshot_schedule_request request) override;
   boost::asio::awaitable<protocol::snapshot_schedule>
   unschedule_snapshot(protocol::snapshot_schedule_id id) override;
   boost::asio::awaitable<protocol::integrity_hash_response> integrity_hash(protocol::admin_query request) override;
   boost::asio::awaitable<bool> schedule_protocol_features(std::vector<protocol::digest> features) override;

   std::atomic<std::uint32_t> calls{0};
   std::atomic<std::uint32_t> error_calls{0};

 private:
   protocol::producer_status_response response_;
};

class write_services {
 public:
   write_services(std::shared_ptr<transaction_implementation> transactions,
                  std::shared_ptr<submission_implementation> submissions,
                  std::shared_ptr<admin_implementation> administration);

   [[nodiscard]] std::shared_ptr<chain_api::transaction> transactions() const;
   [[nodiscard]] std::shared_ptr<chain_api::submission> submissions() const;
   [[nodiscard]] std::shared_ptr<chain_api::admin> administration() const;
   [[nodiscard]] std::uint32_t transaction_calls() const;
   [[nodiscard]] std::uint32_t transaction_await_started() const;
   [[nodiscard]] std::uint32_t transaction_await_deadlines() const;
   [[nodiscard]] std::uint32_t transaction_await_cancellations() const;
   [[nodiscard]] std::uint32_t submission_calls() const;
   [[nodiscard]] std::uint64_t submission_last_timeout_ms() const;
   [[nodiscard]] std::uint64_t submission_last_batch_timeout_ms() const;
   [[nodiscard]] std::uint32_t administration_calls() const;
   [[nodiscard]] std::uint32_t administration_error_calls() const;
   [[nodiscard]] bool transaction_audit_required() const;

 private:
   std::shared_ptr<transaction_implementation> transactions_;
   std::shared_ptr<submission_implementation> submissions_;
   std::shared_ptr<admin_implementation> administration_;
};

struct write_expectations {
   protocol::transaction_read_only_response transaction;
   protocol::producer_status_response administration;
};

[[nodiscard]] write_expectations make_write_expectations();
[[nodiscard]] write_services make_write_services(const write_expectations& expectations);

} // namespace package_chain_api_component
