#include <boost/test/unit_test.hpp>

#include "../../libraries/auth/appauth/details/auth_state_binding.hxx"

#import <AppAuth.h>
#import <Foundation/Foundation.h>

#include <atomic>
#include <barrier>
#include <thread>

BOOST_AUTO_TEST_SUITE(appauth_request_tests)

BOOST_AUTO_TEST_CASE(native_code_flow_uses_state_nonce_and_pkce_s256) {
   @autoreleasepool {
      auto configuration = [[OIDServiceConfiguration alloc]
          initWithAuthorizationEndpoint:[NSURL URLWithString:@"https://issuer.example/authorize"]
                          tokenEndpoint:[NSURL URLWithString:@"https://issuer.example/token"]];
      auto request = [[OIDAuthorizationRequest alloc]
          initWithConfiguration:configuration
                       clientId:@"native-client"
                         scopes:@[ @"openid", @"profile" ]
                    redirectURL:[NSURL URLWithString:@"http://127.0.0.1:49152/callback"]
                   responseType:OIDResponseTypeCode
           additionalParameters:@{@"audience" : @"forge-api"}];

      BOOST_REQUIRE(request != nil);
      BOOST_CHECK(request.clientSecret == nil);
      BOOST_REQUIRE(request.state != nil);
      BOOST_REQUIRE(request.nonce != nil);
      BOOST_REQUIRE(request.codeVerifier != nil);
      BOOST_REQUIRE(request.codeChallenge != nil);
      BOOST_CHECK(request.state.length >= 32U);
      BOOST_CHECK(request.nonce.length >= 32U);
      BOOST_CHECK(request.codeVerifier.length >= 43U);
      BOOST_CHECK(request.codeVerifier.length <= 128U);
      BOOST_CHECK([request.codeChallengeMethod isEqualToString:OIDOAuthorizationRequestCodeChallengeMethodS256]);
      BOOST_CHECK([request.codeChallenge
          isEqualToString:[OIDAuthorizationRequest codeChallengeS256ForVerifier:request.codeVerifier]]);
      BOOST_CHECK([request.additionalParameters[@"audience"] isEqualToString:@"forge-api"]);
   }
}

BOOST_AUTO_TEST_CASE(persisted_state_binding_rejects_immutable_config_mismatch) {
   using forge::auth::appauth::detail::auth_state_config;
   using forge::auth::appauth::detail::canonical_auth_state_binding;

   const auto configured = auth_state_config{
       .issuer = "https://issuer.example",
       .client_id = "native-client",
       .scopes = {"profile", "openid"},
       .audience = "forge-api",
   };
   const auto binding = canonical_auth_state_binding(configured);

   auto reordered_scopes = configured;
   std::swap(reordered_scopes.scopes[0], reordered_scopes.scopes[1]);
   BOOST_CHECK_EQUAL(binding, canonical_auth_state_binding(reordered_scopes));

   auto different_issuer = configured;
   different_issuer.issuer = "https://other-issuer.example";
   BOOST_CHECK_NE(binding, canonical_auth_state_binding(different_issuer));

   auto different_client = configured;
   different_client.client_id = "other-client";
   BOOST_CHECK_NE(binding, canonical_auth_state_binding(different_client));

   auto different_scopes = configured;
   different_scopes.scopes.push_back("email");
   BOOST_CHECK_NE(binding, canonical_auth_state_binding(different_scopes));

   auto different_audience = configured;
   different_audience.audience.reset();
   BOOST_CHECK_NE(binding, canonical_auth_state_binding(different_audience));
}

BOOST_AUTO_TEST_CASE(invalidation_rejects_stale_state_mutation) {
   auto epoch = forge::auth::appauth::detail::auth_state_epoch{};
   const auto stale = epoch.capture();
   auto erased = false;
   auto restored = false;
   auto stale_cleanup = false;

   epoch.invalidate([&] { erased = true; });
   BOOST_CHECK(erased);
   BOOST_CHECK(!epoch.commit_if_current(stale, [&] { restored = true; }));
   BOOST_CHECK(!epoch.commit_if_current(stale, [&] { stale_cleanup = true; }));
   BOOST_CHECK(!restored);
   BOOST_CHECK(!stale_cleanup);

   const auto current = epoch.capture();
   BOOST_CHECK(epoch.commit_if_current(current, [&] { restored = true; }));
   BOOST_CHECK(restored);
   BOOST_CHECK(!epoch.commit_if_current(current, [&] { stale_cleanup = true; }));
   BOOST_CHECK(!stale_cleanup);
}

BOOST_AUTO_TEST_CASE(concurrent_state_mutations_consume_an_epoch_ticket_once) {
   auto epoch = forge::auth::appauth::detail::auth_state_epoch{};
   const auto ticket = epoch.capture();
   auto start = std::barrier{3};
   auto accepted = std::atomic_uint32_t{};
   auto actions = std::atomic_uint32_t{};

   const auto commit = [&] {
      start.arrive_and_wait();
      if (epoch.commit_if_current(ticket, [&] { actions.fetch_add(1U, std::memory_order_relaxed); })) {
         accepted.fetch_add(1U, std::memory_order_relaxed);
      }
   };
   auto first = std::thread{commit};
   auto second = std::thread{commit};
   start.arrive_and_wait();
   first.join();
   second.join();

   BOOST_CHECK_EQUAL(accepted.load(std::memory_order_relaxed), 1U);
   BOOST_CHECK_EQUAL(actions.load(std::memory_order_relaxed), 1U);

   const auto current = epoch.capture();
   BOOST_CHECK(epoch.commit_if_current(current, [&] { actions.fetch_add(1U, std::memory_order_relaxed); }));
   BOOST_CHECK_EQUAL(actions.load(std::memory_order_relaxed), 2U);
}

BOOST_AUTO_TEST_SUITE_END()
