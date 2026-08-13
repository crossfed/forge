#pragma once

namespace forge::net::p2p::dht_query {

struct request {
   dht::key target;
   std::optional<peer_id> target_peer;
   dht::options options;
   std::vector<dht::peer> seeds;
   std::size_t requested_provider_count = 0;
   bool collect_value_responses = false;
};

struct result {
   dht::query_result query;
   std::vector<peer_id> queried;
   std::vector<peer_id> failed;
   std::vector<dht::record> records;
   std::vector<std::pair<peer_id, std::optional<dht::record>>> value_responses;
   bool converged = false;
};

[[nodiscard]] bool has_endpoint(const dht::peer& value) noexcept;
void merge_peer(dht::peer& target, const dht::peer& source);
void merge_known(std::map<peer_id, dht::peer>& known, const dht::peer& value, std::size_t limit,
                 const dht::key& target);
void merge_provider(std::vector<dht::peer>& providers, const dht::peer& value, std::size_t limit);
[[nodiscard]] std::vector<dht::peer> sorted_peers(const std::map<peer_id, dht::peer>& known, const dht::key& target);
[[nodiscard]] std::optional<dht::peer> next_peer(const std::map<peer_id, dht::peer>& known,
                                                 const std::set<peer_id>& attempted, const dht::key& target);
[[nodiscard]] bool closest_peers_queried(const std::map<peer_id, dht::peer>& known, const std::set<peer_id>& queried,
                                         const std::set<peer_id>& failed, const dht::key& target,
                                         std::size_t replication);

struct query_response {
   dht::peer peer;
   std::optional<dht::message> response;
   std::exception_ptr local_error;
   bool failed = false;
};

template <typename Query, typename IsPeerFailure> struct query_callables {
   Query query;
   IsPeerFailure is_peer_failure;
};

template <typename Query, typename Postprocess, typename IsPeerFailure>
boost::asio::awaitable<result>
run_on_strand(request value, std::shared_ptr<query_callables<Query, IsPeerFailure>> callables, Postprocess postprocess,
              boost::asio::strand<boost::asio::any_io_executor> strand) {
   namespace asio = boost::asio;
   using completion_channel = asio::experimental::concurrent_channel<void(boost::system::error_code, query_response)>;

   if (value.options.alpha == 0 || value.options.max_query_peers == 0) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_options,
                            "DHT query concurrency and discovered-peer limits must be positive");
   }
   auto known = std::map<peer_id, dht::peer>{};
   for (const auto& peer : value.seeds) {
      merge_known(known, peer, value.options.max_query_peers, value.target);
   }

   auto attempted = std::set<peer_id>{};
   auto queried = std::set<peer_id>{};
   auto failed = std::set<peer_id>{};
   auto active = std::map<peer_id, std::unique_ptr<asio::cancellation_signal>>{};
   auto out = result{.query = dht::query_result{.target = value.target}};
   const auto alpha = value.options.alpha;
   auto completions = std::make_shared<completion_channel>(strand, alpha);

   const auto launch = [&](dht::peer peer) {
      auto child_cancellation = std::make_unique<asio::cancellation_signal>();
      const auto child_cancellation_slot = child_cancellation->slot();
      const auto id = peer.id;
      active.emplace(id, std::move(child_cancellation));
      try {
         asio::co_spawn(
             strand,
             [callables, completions, peer = std::move(peer)]() mutable -> asio::awaitable<void> {
                auto completed = query_response{.peer = peer};
                try {
                   completed.response = co_await callables->query(peer);
                } catch (const forge::exceptions::base& error) {
                   try {
                      completed.failed = callables->is_peer_failure(peer, error);
                      if (!completed.failed) {
                         completed.local_error = std::current_exception();
                      }
                   } catch (...) {
                      completed.local_error = std::current_exception();
                   }
                } catch (...) {
                   completed.local_error = std::current_exception();
                }
                static_cast<void>(completions->try_send(boost::system::error_code{}, std::move(completed)));
             },
             asio::bind_cancellation_slot(child_cancellation_slot, asio::detached));
      } catch (...) {
         active.erase(id);
         throw;
      }
   };

   const auto cancel_children = [&active]() noexcept {
      for (const auto& [_, cancellation] : active) {
         try {
            cancellation->emit(asio::cancellation_type::all);
         } catch (...) {
            // Completion remains the ownership barrier even if a cancellation callback rejects the signal.
         }
      }
   };

   auto terminal_error = std::exception_ptr{};
   try {
      while (true) {
         while (active.size() < alpha) {
            auto candidate = next_peer(known, attempted, value.target);
            if (!candidate) {
               break;
            }
            attempted.insert(candidate->id);
            launch(std::move(*candidate));
         }

         if (active.empty()) {
            break;
         }

         auto item = co_await completions->async_receive(asio::use_awaitable);
         active.erase(item.peer.id);
         auto application_complete = false;
         if (item.local_error) {
            std::rethrow_exception(item.local_error);
         }
         if (item.failed || !item.response) {
            failed.insert(item.peer.id);
         } else {
            queried.insert(item.peer.id);
            if (value.target_peer && item.peer.id == *value.target_peer) {
               out.query.complete = true;
            }
            if (item.response->record_value) {
               out.records.push_back(*item.response->record_value);
            }
            if (value.collect_value_responses) {
               out.value_responses.emplace_back(item.peer.id, item.response->record_value);
            }
            application_complete = co_await postprocess(item.peer, *item.response);
            if (!application_complete) {
               for (const auto& closer : item.response->closer_peers) {
                  merge_known(known, closer, value.options.max_query_peers, value.target);
                  if (value.target_peer && closer.id == *value.target_peer) {
                     out.query.complete = true;
                  }
               }
               for (const auto& provider : item.response->provider_peers) {
                  merge_provider(out.query.provider_peers, provider, value.requested_provider_count);
               }
            }
         }

         if (out.query.complete || application_complete ||
             (value.requested_provider_count != 0 &&
              out.query.provider_peers.size() >= value.requested_provider_count)) {
            break;
         }
         if (closest_peers_queried(known, queried, failed, value.target, value.options.replication)) {
            out.converged = true;
            break;
         }
      }
   } catch (...) {
      terminal_error = std::current_exception();
   }

   if (!active.empty()) {
      co_await asio::this_coro::reset_cancellation_state(asio::disable_cancellation{});
      cancel_children();
      while (!active.empty()) {
         auto item = co_await completions->async_receive(asio::use_awaitable);
         active.erase(item.peer.id);
      }
   }
   if (terminal_error) {
      std::rethrow_exception(terminal_error);
   }

   for (const auto& peer : failed) {
      known.erase(peer);
   }
   auto closest = sorted_peers(known, value.target);
   if (closest.size() > value.options.replication) {
      closest.resize(value.options.replication);
   }
   out.query.closest_peers = std::move(closest);
   out.queried.assign(queried.begin(), queried.end());
   out.failed.assign(failed.begin(), failed.end());
   co_return out;
}

template <typename Query, typename Postprocess, typename IsPeerFailure>
boost::asio::awaitable<result> run(request value, Query query, Postprocess postprocess, IsPeerFailure is_peer_failure) {
   namespace asio = boost::asio;
   auto executor = asio::any_io_executor{co_await asio::this_coro::executor};
   auto strand = asio::make_strand(executor);
   auto callables =
       std::make_shared<query_callables<Query, IsPeerFailure>>(std::move(query), std::move(is_peer_failure));
   co_return co_await asio::co_spawn(
       strand,
       [value = std::move(value), callables, postprocess = std::move(postprocess),
        strand]() mutable -> asio::awaitable<result> {
          co_return co_await run_on_strand(std::move(value), std::move(callables), std::move(postprocess), strand);
       },
       asio::use_awaitable);
}

template <typename Query, typename IsPeerFailure>
boost::asio::awaitable<result> run(request value, Query query, IsPeerFailure is_peer_failure) {
   auto postprocess = [](const dht::peer&, dht::message&) -> boost::asio::awaitable<bool> { co_return false; };
   co_return co_await run(std::move(value), std::move(query), std::move(postprocess), std::move(is_peer_failure));
}

} // namespace forge::net::p2p::dht_query
