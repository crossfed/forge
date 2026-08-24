#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace forge::chain::api {
class admin;
class block;
class info;
class state;
class submission;
class transaction;
} // namespace forge::chain::api

struct p2p_services {
   std::shared_ptr<forge::chain::api::info> information;
   std::shared_ptr<forge::chain::api::block> blocks;
   std::shared_ptr<forge::chain::api::state> state;
   std::shared_ptr<forge::chain::api::transaction> transactions;
   std::shared_ptr<forge::chain::api::submission> submissions;
   std::shared_ptr<forge::chain::api::admin> administration;
   std::function<std::uint32_t()> state_calls;
   std::function<std::uint32_t()> transaction_await_started;
   std::function<std::uint32_t()> transaction_await_deadlines;
   std::function<std::uint32_t()> transaction_await_cancellations;
};

struct p2p_responses {
   std::vector<std::uint8_t> information;
   std::vector<std::uint8_t> block;
   std::vector<std::uint8_t> state;
   std::vector<std::uint8_t> transaction;
   std::vector<std::uint8_t> administration;
   bool oversized_request_rejected = false;
};

[[nodiscard]] p2p_responses run_p2p_e2e(const p2p_services& services);
