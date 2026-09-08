extern "C" {
#include <oidc-agent/api.h>
}

#include <boost/asio/awaitable.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/test/unit_test.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <future>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

import forge.asio.blocking;
import forge.asio.compute;
import forge.asio.runtime;
import forge.auth.oauth2.exceptions;
import forge.auth.oidc_agent.client;

namespace {

enum class response_kind {
   token,
   error,
   malformed,
};

struct fake_agent_state {
   std::mutex mutex;
   std::condition_variable changed;
   response_kind response = response_kind::token;
   bool block = false;
   bool started = false;
   bool released = false;
   std::size_t calls = 0;
   std::size_t frees = 0;
   bool issuer_request = false;
   std::string selection;
   std::string scopes;
   std::string application_hint;
   std::string audience;
   std::time_t minimum_validity = 0;
};

fake_agent_state fake_agent;

[[nodiscard]] char* duplicate(std::string_view value) {
   auto* result = static_cast<char*>(std::malloc(value.size() + 1U));
   if (result == nullptr) {
      std::abort();
   }
   std::memcpy(result, value.data(), value.size());
   result[value.size()] = '\0';
   return result;
}

void release(char*& value) noexcept {
   if (value == nullptr) {
      return;
   }
   std::memset(value, 0, std::strlen(value));
   std::free(value);
   value = nullptr;
}

void reset_fake(response_kind response = response_kind::token, bool block = false) {
   const auto lock = std::scoped_lock{fake_agent.mutex};
   fake_agent.response = response;
   fake_agent.block = block;
   fake_agent.started = false;
   fake_agent.released = false;
   fake_agent.calls = 0;
   fake_agent.frees = 0;
   fake_agent.issuer_request = false;
   fake_agent.selection.clear();
   fake_agent.scopes.clear();
   fake_agent.application_hint.clear();
   fake_agent.audience.clear();
   fake_agent.minimum_validity = 0;
}

[[nodiscard]] agent_response make_response(const char* selection, std::time_t minimum_validity, const char* scopes,
                                           const char* application_hint, const char* audience, bool issuer_request) {
   auto response = response_kind::token;
   {
      auto lock = std::unique_lock{fake_agent.mutex};
      ++fake_agent.calls;
      fake_agent.started = true;
      fake_agent.issuer_request = issuer_request;
      fake_agent.selection = selection == nullptr ? "" : selection;
      fake_agent.scopes = scopes == nullptr ? "" : scopes;
      fake_agent.application_hint = application_hint == nullptr ? "" : application_hint;
      fake_agent.audience = audience == nullptr ? "" : audience;
      fake_agent.minimum_validity = minimum_validity;
      response = fake_agent.response;
      fake_agent.changed.notify_all();
      fake_agent.changed.wait(lock, [] { return !fake_agent.block || fake_agent.released; });
   }

   auto result = agent_response{};
   result.type = response == response_kind::error ? AGENT_RESPONSE_TYPE_ERROR : AGENT_RESPONSE_TYPE_TOKEN;
   if (response == response_kind::error) {
      result.error_response.error = duplicate("setup required");
      result.error_response.help = duplicate("run oidc-gen");
   } else if (response == response_kind::token) {
      result.token_response.token = duplicate("test-access-token");
      result.token_response.issuer = duplicate("https://issuer.example");
      result.token_response.expires_at = std::time(nullptr) + 3600;
   }
   return result;
}

[[nodiscard]] std::shared_ptr<forge::auth::oidc_agent::client>
make_client(forge::auth::oidc_agent::selection_kind kind, forge::asio::compute::executor blocking_executor) {
   return forge::auth::oidc_agent::client::create(
       {
           .selection = {.kind = kind,
                         .value = kind == forge::auth::oidc_agent::selection_kind::account ? "test-account"
                                                                                           : "https://issuer.example"},
           .application_hint = "forge-tests",
       },
       std::move(blocking_executor));
}

[[nodiscard]] forge::asio::compute::pool::options blocking_lane_options() {
   return {
       .worker_threads = 1,
       .max_pending_tasks = 0,
       .max_waiting_submissions = 32,
       .thread_name = "forge-oidc-agent-test",
   };
}

} // namespace

extern "C" agent_response getAgentTokenResponse(const char* account, std::time_t minimum_validity, const char* scopes,
                                                const char* application_hint, const char* audience) {
   return make_response(account, minimum_validity, scopes, application_hint, audience, false);
}

extern "C" agent_response getAgentTokenResponseForIssuer(const char* issuer, std::time_t minimum_validity,
                                                         const char* scopes, const char* application_hint,
                                                         const char* audience) {
   return make_response(issuer, minimum_validity, scopes, application_hint, audience, true);
}

extern "C" void secFreeAgentResponse(agent_response response) {
   if (response.type == AGENT_RESPONSE_TYPE_TOKEN) {
      release(response.token_response.token);
      release(response.token_response.issuer);
   } else if (response.type == AGENT_RESPONSE_TYPE_ERROR) {
      release(response.error_response.error);
      release(response.error_response.help);
   }
   const auto lock = std::scoped_lock{fake_agent.mutex};
   ++fake_agent.frees;
   fake_agent.changed.notify_all();
}

BOOST_AUTO_TEST_SUITE(oidc_agent_tests)

BOOST_AUTO_TEST_CASE(account_and_issuer_requests_use_the_public_agent_api) {
   auto blocking = forge::asio::compute::pool{blocking_lane_options()};
   for (const auto kind :
        {forge::auth::oidc_agent::selection_kind::account, forge::auth::oidc_agent::selection_kind::issuer}) {
      reset_fake();
      auto runtime = forge::asio::runtime{};
      auto provider = make_client(kind, blocking.get_executor());
      auto token = forge::asio::blocking::run(runtime, provider->async_access_token({
                                                           .scopes = {"openid", "profile"},
                                                           .audience = "forge-api",
                                                           .minimum_validity = std::chrono::seconds{60},
                                                       }));

      BOOST_CHECK(token.secret().view() == std::string_view{"test-access-token"});
      BOOST_CHECK_EQUAL(token.metadata().issuer, "https://issuer.example");
      const auto lock = std::scoped_lock{fake_agent.mutex};
      BOOST_CHECK_EQUAL(fake_agent.calls, 1U);
      BOOST_CHECK_EQUAL(fake_agent.frees, 1U);
      BOOST_CHECK_EQUAL(fake_agent.issuer_request, kind == forge::auth::oidc_agent::selection_kind::issuer);
      BOOST_CHECK_EQUAL(fake_agent.scopes, "openid profile");
      BOOST_CHECK_EQUAL(fake_agent.application_hint, "forge-tests");
      BOOST_CHECK_EQUAL(fake_agent.audience, "forge-api");
      BOOST_CHECK_EQUAL(fake_agent.minimum_validity, 60);
   }
}

BOOST_AUTO_TEST_CASE(agent_errors_and_malformed_tokens_fail_closed) {
   auto blocking = forge::asio::compute::pool{blocking_lane_options()};
   auto runtime = forge::asio::runtime{};
   auto provider = make_client(forge::auth::oidc_agent::selection_kind::account, blocking.get_executor());

   reset_fake(response_kind::error);
   BOOST_CHECK_THROW((void)forge::asio::blocking::run(runtime, provider->async_access_token({})),
                     forge::auth::oauth2::exceptions::setup_required);

   reset_fake(response_kind::malformed);
   BOOST_CHECK_THROW((void)forge::asio::blocking::run(runtime, provider->async_access_token({})),
                     forge::auth::oidc_agent::exceptions::invalid_response);
}

BOOST_AUTO_TEST_CASE(empty_blocking_executor_is_rejected) {
   reset_fake();
   BOOST_CHECK_THROW(
       (void)forge::auth::oidc_agent::client::create(
           {
               .selection = {.kind = forge::auth::oidc_agent::selection_kind::account, .value = "test-account"},
               .application_hint = "forge-tests",
           },
           {}),
       forge::auth::oidc_agent::exceptions::invalid_options);

   const auto lock = std::scoped_lock{fake_agent.mutex};
   BOOST_CHECK_EQUAL(fake_agent.calls, 0U);
}

BOOST_AUTO_TEST_CASE(invalid_scope_never_reaches_the_agent) {
   reset_fake();
   auto blocking = forge::asio::compute::pool{blocking_lane_options()};
   auto runtime = forge::asio::runtime{};
   auto provider = make_client(forge::auth::oidc_agent::selection_kind::account, blocking.get_executor());
   auto invalid_scope = std::string{"open\0id", 7U};

   BOOST_CHECK_THROW(
       (void)forge::asio::blocking::run(runtime, provider->async_access_token({.scopes = {std::move(invalid_scope)}})),
       forge::auth::oauth2::exceptions::invalid_request);
   const auto lock = std::scoped_lock{fake_agent.mutex};
   BOOST_CHECK_EQUAL(fake_agent.calls, 0U);
}

BOOST_AUTO_TEST_CASE(cancellation_releases_the_caller_before_the_agent_returns) {
   reset_fake(response_kind::token, true);
   auto blocking = forge::asio::compute::pool{blocking_lane_options()};
   auto runtime = forge::asio::runtime{};
   auto provider = make_client(forge::auth::oidc_agent::selection_kind::account, blocking.get_executor());
   auto cancellation = boost::asio::cancellation_signal{};
   auto request = [provider]() -> boost::asio::awaitable<void> {
      static_cast<void>(co_await provider->async_access_token({}));
   };
   auto future = boost::asio::co_spawn(
       runtime.context(), request(), boost::asio::bind_cancellation_slot(cancellation.slot(), boost::asio::use_future));

   {
      auto lock = std::unique_lock{fake_agent.mutex};
      BOOST_REQUIRE(fake_agent.changed.wait_for(lock, std::chrono::seconds{2}, [] { return fake_agent.started; }));
   }
   cancellation.emit(boost::asio::cancellation_type::all);
   BOOST_REQUIRE(future.wait_for(std::chrono::seconds{1}) == std::future_status::ready);
   BOOST_CHECK_THROW((void)future.get(), forge::auth::oauth2::exceptions::canceled);

   {
      const auto lock = std::scoped_lock{fake_agent.mutex};
      BOOST_CHECK_EQUAL(fake_agent.frees, 0U);
   }
   {
      const auto lock = std::scoped_lock{fake_agent.mutex};
      fake_agent.released = true;
      fake_agent.changed.notify_all();
   }
   {
      auto lock = std::unique_lock{fake_agent.mutex};
      BOOST_REQUIRE(fake_agent.changed.wait_for(lock, std::chrono::seconds{2}, [] { return fake_agent.frees == 1U; }));
   }
}

BOOST_AUTO_TEST_CASE(cancellation_while_queued_uses_the_oauth_error_contract) {
   reset_fake(response_kind::token, true);
   auto blocking = forge::asio::compute::pool{blocking_lane_options()};
   auto runtime = forge::asio::runtime{};
   auto provider = make_client(forge::auth::oidc_agent::selection_kind::account, blocking.get_executor());
   auto request = [provider]() -> boost::asio::awaitable<void> {
      static_cast<void>(co_await provider->async_access_token({}));
   };
   auto blocker = boost::asio::co_spawn(runtime.context(), request(), boost::asio::use_future);

   {
      auto lock = std::unique_lock{fake_agent.mutex};
      BOOST_REQUIRE(fake_agent.changed.wait_for(lock, std::chrono::seconds{2}, [] { return fake_agent.started; }));
   }

   auto cancellation = boost::asio::cancellation_signal{};
   auto queued = boost::asio::co_spawn(
       runtime.context(), request(), boost::asio::bind_cancellation_slot(cancellation.slot(), boost::asio::use_future));
   cancellation.emit(boost::asio::cancellation_type::all);

   BOOST_REQUIRE(queued.wait_for(std::chrono::seconds{1}) == std::future_status::ready);
   BOOST_CHECK_THROW((void)queued.get(), forge::auth::oauth2::exceptions::canceled);
   {
      const auto lock = std::scoped_lock{fake_agent.mutex};
      BOOST_CHECK_EQUAL(fake_agent.calls, 1U);
      fake_agent.released = true;
      fake_agent.changed.notify_all();
   }
   BOOST_CHECK_NO_THROW((void)blocker.get());
}

BOOST_AUTO_TEST_CASE(saturated_submission_queue_uses_the_oidc_agent_error_contract) {
   reset_fake(response_kind::token, true);
   auto blocking = forge::asio::compute::pool{blocking_lane_options()};
   auto runtime = forge::asio::runtime{};
   auto provider = make_client(forge::auth::oidc_agent::selection_kind::account, blocking.get_executor());
   auto request = [provider]() -> boost::asio::awaitable<void> {
      static_cast<void>(co_await provider->async_access_token({}));
   };
   auto blocker = boost::asio::co_spawn(runtime.context(), request(), boost::asio::use_future);

   {
      auto lock = std::unique_lock{fake_agent.mutex};
      BOOST_REQUIRE(fake_agent.changed.wait_for(lock, std::chrono::seconds{2}, [] { return fake_agent.started; }));
   }

   auto requests = std::vector<std::future<void>>{};
   requests.reserve(40U);
   for (auto index = 0U; index < 40U; ++index) {
      requests.push_back(boost::asio::co_spawn(runtime.context(), request(), boost::asio::use_future));
   }

   auto ready = std::size_t{0};
   const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
   while (ready < 8U && std::chrono::steady_clock::now() < deadline) {
      ready = 0U;
      for (auto& request : requests) {
         ready += request.wait_for(std::chrono::seconds{0}) == std::future_status::ready ? 1U : 0U;
      }
      std::this_thread::yield();
   }
   BOOST_REQUIRE_EQUAL(ready, 8U);

   {
      const auto lock = std::scoped_lock{fake_agent.mutex};
      fake_agent.released = true;
      fake_agent.changed.notify_all();
   }

   BOOST_CHECK_NO_THROW((void)blocker.get());
   auto unavailable = std::size_t{0};
   for (auto& request : requests) {
      try {
         static_cast<void>(request.get());
      } catch (const forge::auth::oidc_agent::exceptions::unavailable&) {
         ++unavailable;
      }
   }
   BOOST_CHECK_EQUAL(unavailable, 8U);
}

BOOST_AUTO_TEST_SUITE_END()
