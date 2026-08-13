#include <boost/test/unit_test.hpp>
#include <boost/asio/awaitable.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

import forge.asio.blocking;
import forge.asio.runtime;
import forge.multiformats.multihash;
import forge.multiformats.types;
import forge.net.p2p.endpoint;
import forge.net.p2p.dht.record_store;
import forge.net.p2p.exceptions;
import forge.net.p2p.identity;
import forge.net.p2p.protocol;
import forge.test.net.p2p.dht_record_store_fixture;

namespace forge::net::p2p {
namespace {

[[nodiscard]] peer_id test_peer(std::uint8_t value) {
   const auto payload = forge::multiformats::bytes{value};
   return peer_id::from_bytes(forge::multiformats::multihash::identity(payload).encode());
}

[[nodiscard]] dht::key test_key(std::uint8_t value) {
   return dht::key{.bytes = {'/', value}};
}

[[nodiscard]] dht::profile test_profile() {
   return custom_dht_profile(
       protocol_id{.value = "/forge/test/kad/1.0.0"}, dht::mode::server,
       dht::profile_capabilities{.peers = true, .providers = true, .values = true},
       {dht::value_policy{
           .key_prefix = {'/'},
           .validate =
               [](const dht::record& value, dht::value_validation_context) {
                  if (value.value.empty()) {
                     throw std::runtime_error{"empty test DHT value"};
                  }
               },
           .select =
               [](std::span<const dht::record> candidates) {
                  if (candidates.empty()) {
                     throw std::runtime_error{"empty test DHT selection"};
                  }
                  auto selected = std::size_t{};
                  for (auto index = std::size_t{1}; index < candidates.size(); ++index) {
                     if (candidates[selected].value < candidates[index].value) {
                        selected = index;
                     }
                  }
                  return selected;
               },
           .expiry = [](const dht::record&, dht::value_expiry_context context) { return context.supplied_expires_at; },
       }});
}

[[nodiscard]] dht::record_store::value_record test_value(dht::key key, std::uint8_t value,
                                                         std::chrono::system_clock::time_point expires_at) {
   return dht::record_store::value_record{
       .record = dht::record{.key_value = std::move(key), .value = {value}},
       .expires_at = expires_at,
   };
}

[[nodiscard]] dht::record_store::provider_record
test_provider(dht::key key, peer_id provider, std::chrono::system_clock::time_point provider_expires_at,
              std::chrono::system_clock::time_point addresses_expires_at, std::uint16_t port = 4401) {
   auto address = parse_endpoint("/ip4/127.0.0.1/udp/" + std::to_string(port) + "/quic-v1/p2p/" + provider.to_string());
   return dht::record_store::provider_record{
       .key = std::move(key),
       .provider = std::move(provider),
       .endpoints = {std::move(address)},
       .provider_expires_at = provider_expires_at,
       .addresses_expires_at = addresses_expires_at,
   };
}

} // namespace

BOOST_AUTO_TEST_SUITE(dht_record_store_tests)

BOOST_AUTO_TEST_CASE(dht_record_store_validates_and_selects_before_durable_publication) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto store = dht::record_store{test_profile()};
   const auto now = std::chrono::system_clock::now();
   const auto key = test_key('s');

   const auto first =
       forge::asio::blocking::run(runtime, store.async_put(test_value(key, 2, now + std::chrono::hours{1}), now));
   BOOST_TEST(first.selected.record.value == (std::vector<std::uint8_t>{2}));
   BOOST_TEST(static_cast<int>(first.outcome) == static_cast<int>(dht::record_store::put_outcome::incoming_stored));
   const auto rejected =
       forge::asio::blocking::run(runtime, store.async_put(test_value(key, 1, now + std::chrono::hours{2}), now));
   BOOST_TEST(rejected.selected.record.value == (std::vector<std::uint8_t>{2}));
   BOOST_TEST(static_cast<int>(rejected.outcome) ==
              static_cast<int>(dht::record_store::put_outcome::existing_preferred));
   const auto selected =
       forge::asio::blocking::run(runtime, store.async_put(test_value(key, 3, now + std::chrono::hours{3}), now));
   BOOST_TEST(selected.selected.record.value == (std::vector<std::uint8_t>{3}));
   BOOST_TEST(static_cast<int>(selected.outcome) == static_cast<int>(dht::record_store::put_outcome::incoming_stored));
   BOOST_TEST(store.find_value(key, now)->record.value == (std::vector<std::uint8_t>{3}));
}

BOOST_AUTO_TEST_CASE(dht_record_store_rolls_back_operational_value_when_persistence_throws) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto persistence = std::make_shared<forge::test::net::p2p::dht_record_store_persistence>();
   auto store = dht::record_store{test_profile(), dht::record_store::options{.persistence = persistence}};
   const auto now = std::chrono::system_clock::now();
   const auto key = test_key('r');
   forge::asio::blocking::run(runtime, store.async_put(test_value(key, 1, now + std::chrono::hours{1}), now));

   persistence->fail_next_apply();
   BOOST_CHECK_THROW(
       (forge::asio::blocking::run(runtime, store.async_put(test_value(key, 2, now + std::chrono::hours{2}), now))),
       std::runtime_error);
   const auto stored = store.find_value(key, now);
   BOOST_REQUIRE(stored);
   BOOST_TEST(stored->record.value == (std::vector<std::uint8_t>{1}));
   const auto status = store.persistence_state();
   BOOST_TEST(status.failure_count == 1U);
   BOOST_TEST(status.degraded);
   BOOST_TEST(!status.durability_uncertain);
   BOOST_TEST(status.last_failure.find("injected DHT record persistence failure") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(dht_record_store_publishes_post_commit_uncertain_value_and_flush_recovers_status) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto persistence = std::make_shared<forge::test::net::p2p::dht_record_store_persistence>();
   auto store = dht::record_store{test_profile(), dht::record_store::options{.persistence = persistence}};
   const auto now = std::chrono::system_clock::now();
   const auto key = test_key('u');

   persistence->make_next_apply_durability_uncertain();
   BOOST_CHECK_THROW(
       (forge::asio::blocking::run(runtime, store.async_put(test_value(key, 7, now + std::chrono::hours{1}), now))),
       exceptions::durability_uncertain);

   const auto operational = store.find_value(key, now);
   BOOST_REQUIRE(operational);
   BOOST_TEST(operational->record.value == (std::vector<std::uint8_t>{7}));
   const auto durable =
       forge::asio::blocking::run(runtime, persistence->async_hydrate(dht::record_store::hydration_request{
                                               .kind = dht::record_store::hydration_kind::values}));
   BOOST_REQUIRE_EQUAL(durable.values.size(), 1U);
   BOOST_TEST(durable.values.front().record.value == (std::vector<std::uint8_t>{7}));

   auto status = store.persistence_state();
   BOOST_TEST(status.failure_count == 1U);
   BOOST_TEST(status.degraded);
   BOOST_TEST(status.durability_uncertain);
   BOOST_TEST(status.last_failure.find("post-commit DHT durability failure") != std::string::npos);

   forge::asio::blocking::run(runtime, store.async_flush());
   status = store.persistence_state();
   BOOST_TEST(status.failure_count == 1U);
   BOOST_TEST(!status.degraded);
   BOOST_TEST(!status.durability_uncertain);
   BOOST_TEST(status.last_failure.empty());
}

BOOST_AUTO_TEST_CASE(dht_record_store_publishes_post_commit_uncertain_provider) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto persistence = std::make_shared<forge::test::net::p2p::dht_record_store_persistence>();
   auto store = dht::record_store{test_profile(), dht::record_store::options{.persistence = persistence}};
   const auto now = std::chrono::system_clock::now();
   const auto key = test_key('o');
   const auto provider = test_peer(11);

   persistence->make_next_apply_durability_uncertain();
   BOOST_CHECK_THROW((forge::asio::blocking::run(
                         runtime, store.async_upsert_provider(test_provider(key, provider, now + std::chrono::hours{1},
                                                                            now + std::chrono::minutes{5}),
                                                              now))),
                     exceptions::durability_uncertain);

   const auto operational = store.find_providers(key, 1, now);
   BOOST_REQUIRE_EQUAL(operational.size(), 1U);
   BOOST_CHECK(operational.front().provider == provider);
   const auto durable =
       forge::asio::blocking::run(runtime, persistence->async_hydrate(dht::record_store::hydration_request{
                                               .kind = dht::record_store::hydration_kind::providers}));
   BOOST_REQUIRE_EQUAL(durable.providers.size(), 1U);
   BOOST_CHECK(durable.providers.front().provider == provider);
   BOOST_TEST(store.persistence_state().degraded);
   BOOST_TEST(store.persistence_state().durability_uncertain);
}

BOOST_AUTO_TEST_CASE(dht_record_store_publishes_post_commit_uncertain_provider_removal) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto persistence = std::make_shared<forge::test::net::p2p::dht_record_store_persistence>();
   auto store = dht::record_store{test_profile(), dht::record_store::options{.persistence = persistence}};
   const auto now = std::chrono::system_clock::now();
   const auto key = test_key('d');
   const auto provider = test_peer(12);
   forge::asio::blocking::run(
       runtime, store.async_upsert_provider(
                    test_provider(key, provider, now + std::chrono::hours{1}, now + std::chrono::minutes{5}), now));

   persistence->make_next_apply_durability_uncertain();
   BOOST_CHECK_THROW((forge::asio::blocking::run(runtime, store.async_remove_provider(dht::record_store::provider_key{
                                                              .key = key, .provider = provider}))),
                     exceptions::durability_uncertain);

   BOOST_TEST(store.find_providers(key, 1, now).empty());
   const auto durable =
       forge::asio::blocking::run(runtime, persistence->async_hydrate(dht::record_store::hydration_request{
                                               .kind = dht::record_store::hydration_kind::providers}));
   BOOST_TEST(durable.providers.empty());
   BOOST_TEST(store.persistence_state().degraded);
   BOOST_TEST(store.persistence_state().durability_uncertain);
}

BOOST_AUTO_TEST_CASE(dht_record_store_removes_durable_provider_absent_from_operational_state) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto persistence = std::make_shared<forge::test::net::p2p::dht_record_store_persistence>();
   auto store = dht::record_store{test_profile(), dht::record_store::options{.persistence = persistence}};
   const auto now = std::chrono::system_clock::now();
   const auto key = test_key('b');
   const auto provider = test_peer(13);
   auto batch = dht::record_store::mutation_batch{};
   batch.provider_upserts.push_back(
       test_provider(key, provider, now + std::chrono::hours{1}, now + std::chrono::minutes{5}));
   const auto seeded = forge::asio::blocking::run(runtime, persistence->async_apply(std::move(batch)));
   BOOST_TEST(seeded.durability_confirmed);
   BOOST_TEST(store.find_providers(key, 1, now).empty());

   forge::asio::blocking::run(
       runtime, store.async_remove_provider(dht::record_store::provider_key{.key = key, .provider = provider}));

   const auto durable =
       forge::asio::blocking::run(runtime, persistence->async_hydrate(dht::record_store::hydration_request{
                                               .kind = dht::record_store::hydration_kind::providers}));
   BOOST_TEST(durable.providers.empty());
}

BOOST_AUTO_TEST_CASE(dht_record_store_publishes_post_commit_uncertain_prune) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto persistence = std::make_shared<forge::test::net::p2p::dht_record_store_persistence>();
   auto store = dht::record_store{test_profile(), dht::record_store::options{.persistence = persistence}};
   const auto now = std::chrono::system_clock::now();
   const auto key = test_key('n');
   forge::asio::blocking::run(runtime, store.async_put(test_value(key, 9, now + std::chrono::minutes{1}), now));

   persistence->make_next_prune_durability_uncertain();
   BOOST_CHECK_THROW((forge::asio::blocking::run(runtime, store.async_prune_expired(now + std::chrono::minutes{1}))),
                     exceptions::durability_uncertain);

   BOOST_TEST(!store.find_value(key, now + std::chrono::minutes{1}));
   const auto durable =
       forge::asio::blocking::run(runtime, persistence->async_hydrate(dht::record_store::hydration_request{
                                               .kind = dht::record_store::hydration_kind::values}));
   BOOST_TEST(durable.values.empty());
   const auto status = store.persistence_state();
   BOOST_TEST(status.failure_count == 1U);
   BOOST_TEST(status.degraded);
   BOOST_TEST(status.durability_uncertain);
   BOOST_TEST(status.last_failure.find("post-commit DHT prune durability failure") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(dht_record_store_keeps_provider_identity_after_address_expiry) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto store = dht::record_store{test_profile()};
   const auto now = std::chrono::system_clock::now();
   const auto key = test_key('p');
   const auto provider = test_peer(1);
   forge::asio::blocking::run(
       runtime, store.async_upsert_provider(
                    test_provider(key, provider, now + std::chrono::minutes{2}, now + std::chrono::minutes{1}), now));

   const auto without_addresses = store.find_providers(key, 1, now + std::chrono::minutes{1});
   BOOST_REQUIRE_EQUAL(without_addresses.size(), 1U);
   BOOST_CHECK(without_addresses.front().provider == provider);
   BOOST_TEST(without_addresses.front().endpoints.empty());
   BOOST_TEST(store.find_providers(key, 1, now + std::chrono::minutes{2}).empty());
}

BOOST_AUTO_TEST_CASE(dht_record_store_enforces_value_provider_and_per_key_capacity) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   const auto now = std::chrono::system_clock::now();
   auto store = dht::record_store{
       test_profile(), dht::record_store::options{.max_values = 1, .max_providers = 2, .max_providers_per_key = 1}};

   forge::asio::blocking::run(runtime, store.async_put(test_value(test_key('a'), 1, now + std::chrono::hours{1}), now));
   BOOST_CHECK_THROW((forge::asio::blocking::run(
                         runtime, store.async_put(test_value(test_key('b'), 1, now + std::chrono::hours{1}), now))),
                     exceptions::backpressure_rejected);

   const auto first_key = test_key('c');
   forge::asio::blocking::run(
       runtime,
       store.async_upsert_provider(
           test_provider(first_key, test_peer(2), now + std::chrono::hours{1}, now + std::chrono::minutes{5}), now));
   BOOST_CHECK_THROW(
       (forge::asio::blocking::run(
           runtime, store.async_upsert_provider(test_provider(first_key, test_peer(3), now + std::chrono::hours{1},
                                                              now + std::chrono::minutes{5}),
                                                now))),
       exceptions::backpressure_rejected);
   forge::asio::blocking::run(
       runtime, store.async_upsert_provider(test_provider(test_key('d'), test_peer(3), now + std::chrono::hours{1},
                                                          now + std::chrono::minutes{5}),
                                            now));
   BOOST_CHECK_THROW(
       (forge::asio::blocking::run(
           runtime, store.async_upsert_provider(test_provider(test_key('e'), test_peer(4), now + std::chrono::hours{1},
                                                              now + std::chrono::minutes{5}),
                                                now))),
       exceptions::backpressure_rejected);
}

BOOST_AUTO_TEST_CASE(dht_record_store_enforces_record_and_total_byte_capacity) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   const auto now = std::chrono::system_clock::now();
   auto record_limited = dht::record_store{test_profile(), dht::record_store::options{.max_record_bytes = 2}};
   BOOST_CHECK_THROW(
       (forge::asio::blocking::run(
           runtime, record_limited.async_put(test_value(test_key('x'), 1, now + std::chrono::hours{1}), now))),
       exceptions::backpressure_rejected);

   auto total_limited =
       dht::record_store{test_profile(), dht::record_store::options{.max_total_bytes = 5, .max_record_bytes = 4}};
   forge::asio::blocking::run(runtime,
                              total_limited.async_put(test_value(test_key('y'), 1, now + std::chrono::hours{1}), now));
   BOOST_CHECK_THROW(
       (forge::asio::blocking::run(
           runtime, total_limited.async_put(test_value(test_key('z'), 1, now + std::chrono::hours{1}), now))),
       exceptions::backpressure_rejected);
}

BOOST_AUTO_TEST_CASE(dht_record_store_hydrates_bounded_pages_across_reopen) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto persistence = dht::record_store::make_memory_persistence();
   const auto now = std::chrono::system_clock::now();
   {
      auto writer = dht::record_store{test_profile(), dht::record_store::options{.persistence = persistence}};
      for (auto index = std::uint8_t{}; index < 3; ++index) {
         forge::asio::blocking::run(runtime,
                                    writer.async_put(test_value(test_key(static_cast<std::uint8_t>('h' + index)),
                                                                index + 1, now + std::chrono::hours{1}),
                                                     now));
      }
      forge::asio::blocking::run(
          runtime, writer.async_upsert_provider(test_provider(test_key('q'), test_peer(5), now + std::chrono::hours{1},
                                                              now + std::chrono::minutes{5}),
                                                now));
      forge::asio::blocking::run(runtime, writer.async_flush());
   }

   auto reader = dht::record_store{test_profile(),
                                   dht::record_store::options{.persistence = persistence, .hydration_page_limit = 1}};
   forge::asio::blocking::run(runtime, reader.async_hydrate(now));
   BOOST_REQUIRE(reader.find_value(test_key('h'), now));
   BOOST_REQUIRE(reader.find_value(test_key('i'), now));
   BOOST_REQUIRE(reader.find_value(test_key('j'), now));
   BOOST_REQUIRE_EQUAL(reader.find_providers(test_key('q'), 1, now).size(), 1U);
}

BOOST_AUTO_TEST_CASE(dht_record_store_requires_local_provider_reconfirmation_after_restart) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto persistence = dht::record_store::make_memory_persistence();
   const auto now = std::chrono::system_clock::now();
   const auto key = test_key('r');
   const auto local = test_peer(71);
   const auto remote = test_peer(72);
   {
      auto store = dht::record_store{test_profile(), dht::record_store::options{.persistence = persistence}};
      auto local_record = test_provider(key, local, now + std::chrono::hours{48}, now + std::chrono::hours{24});
      local_record.local_owned = true;
      forge::asio::blocking::run(runtime, store.async_upsert_provider(std::move(local_record), now));
      forge::asio::blocking::run(
          runtime, store.async_upsert_provider(
                       test_provider(key, remote, now + std::chrono::hours{48}, now + std::chrono::hours{24}), now));
      BOOST_TEST(store.find_providers(key, 20, now).size() == 2U);
      // Simulate process loss: no graceful close/withdrawal owns this transition.
   }

   auto reopened = dht::record_store{test_profile(), dht::record_store::options{.persistence = persistence}};
   forge::asio::blocking::run(runtime, reopened.async_hydrate(now));
   const auto providers = reopened.find_providers(key, 20, now);
   BOOST_REQUIRE_EQUAL(providers.size(), 1U);
   BOOST_TEST(providers.front().provider.to_string() == remote.to_string());
   BOOST_TEST(!providers.front().local_owned);

   const auto durable =
       forge::asio::blocking::run(runtime, persistence->async_hydrate(dht::record_store::hydration_request{
                                               .kind = dht::record_store::hydration_kind::providers,
                                               .limit = 20,
                                           }));
   BOOST_REQUIRE_EQUAL(durable.providers.size(), 1U);
   BOOST_TEST(durable.providers.front().provider.to_string() == remote.to_string());
}

BOOST_AUTO_TEST_CASE(dht_record_store_prunes_expiry_in_bounded_steps_and_closes_idempotently) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto store = dht::record_store{test_profile(), dht::record_store::options{.prune_page_limit = 1}};
   const auto now = std::chrono::system_clock::now();
   const auto value_key = test_key('v');
   const auto provider_key = test_key('w');
   forge::asio::blocking::run(runtime, store.async_put(test_value(value_key, 1, now + std::chrono::minutes{1}), now));
   forge::asio::blocking::run(
       runtime, store.async_upsert_provider(test_provider(provider_key, test_peer(6), now + std::chrono::minutes{2},
                                                          now + std::chrono::minutes{1}),
                                            now));

   const auto values = forge::asio::blocking::run(runtime, store.async_prune_expired(now + std::chrono::minutes{1}));
   BOOST_REQUIRE_EQUAL(values.values.size(), 1U);
   BOOST_TEST(values.may_have_more);
   BOOST_TEST(values.durability.durability_confirmed);
   BOOST_TEST(!store.find_value(value_key, now + std::chrono::minutes{1}));
   const auto addresses = forge::asio::blocking::run(runtime, store.async_prune_expired(now + std::chrono::minutes{1}));
   BOOST_REQUIRE_EQUAL(addresses.provider_address_updates.size(), 1U);
   BOOST_TEST(!addresses.may_have_more);
   BOOST_REQUIRE_EQUAL(store.find_providers(provider_key, 1, now + std::chrono::minutes{1}).size(), 1U);
   BOOST_TEST(store.find_providers(provider_key, 1, now + std::chrono::minutes{1}).front().endpoints.empty());
   const auto providers = forge::asio::blocking::run(runtime, store.async_prune_expired(now + std::chrono::minutes{2}));
   BOOST_REQUIRE_EQUAL(providers.providers.size(), 1U);
   BOOST_TEST(store.find_providers(provider_key, 1, now + std::chrono::minutes{2}).empty());

   forge::asio::blocking::run(runtime, store.async_close());
   forge::asio::blocking::run(runtime, store.async_close());
   BOOST_CHECK_THROW((forge::asio::blocking::run(runtime, store.async_flush())), exceptions::closed);
}

BOOST_AUTO_TEST_CASE(dht_record_store_close_retries_after_flush_failure) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto persistence = std::make_shared<forge::test::net::p2p::dht_record_store_persistence>();
   auto store = dht::record_store{test_profile(), {.persistence = persistence}};

   persistence->fail_next_flush();
   BOOST_CHECK_THROW((forge::asio::blocking::run(runtime, store.async_close())), exceptions::durability_uncertain);
   BOOST_TEST(store.persistence_state().closing);
   BOOST_TEST(!store.persistence_state().closed);

   forge::asio::blocking::run(runtime, store.async_close());
   BOOST_TEST(store.persistence_state().closed);
}

BOOST_AUTO_TEST_CASE(dht_record_store_close_retries_after_backend_close_failure) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto persistence = std::make_shared<forge::test::net::p2p::dht_record_store_persistence>();
   auto store = dht::record_store{test_profile(), {.persistence = persistence}};

   persistence->fail_next_close();
   BOOST_CHECK_THROW((forge::asio::blocking::run(runtime, store.async_close())), std::runtime_error);
   BOOST_TEST(store.persistence_state().closing);
   BOOST_TEST(!store.persistence_state().closed);

   forge::asio::blocking::run(runtime, store.async_close());
   BOOST_TEST(store.persistence_state().closed);
}

BOOST_AUTO_TEST_SUITE_END()

} // namespace forge::net::p2p
