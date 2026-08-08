module;

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/experimental/concurrent_channel.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/error_code.hpp>

module forge.net.p2p.node;

import forge.net.p2p.exceptions;
import forge.net.p2p.identity;

#include "details/connection_singleflight_registry.hxx"

namespace forge::net::p2p::detail {

connection_singleflight_registry::lease::lease(peer_id peer, std::shared_ptr<entry> owner,
                                               std::shared_ptr<completion_channel> completion, bool leader)
    : peer_(std::move(peer)), owner_(std::move(owner)), completion_(std::move(completion)), leader_(leader) {}

bool connection_singleflight_registry::lease::leader() const noexcept {
   return leader_;
}

boost::asio::awaitable<connection_singleflight_registry::outcome> connection_singleflight_registry::lease::wait() {
   if (!completion_) {
      co_return outcome{
          .error = exceptions::code::internal,
          .message = "invalid P2P connection singleflight participant",
      };
   }
   co_return co_await completion_->async_receive(boost::asio::use_awaitable);
}

connection_singleflight_registry::lease connection_singleflight_registry::join(const peer_id& peer,
                                                                               boost::asio::any_io_executor executor) {
   auto& current = entries_[peer];
   const auto leader = !current;
   if (!current) {
      current = std::make_shared<entry>();
   }
   auto completion = std::make_shared<lease::completion_channel>(std::move(executor), 1);
   ++current->participants;
   current->completions.push_back(completion);
   if (current->completed) {
      static_cast<void>(completion->try_send(boost::system::error_code{}, current->result));
   }
   return lease{peer, current, std::move(completion), leader};
}

void connection_singleflight_registry::complete(entry& owner, outcome result) noexcept {
   if (owner.completed) {
      return;
   }
   owner.result = std::move(result);
   owner.completed = true;
   for (auto& pending : owner.completions) {
      if (auto completion = pending.lock()) {
         static_cast<void>(completion->try_send(boost::system::error_code{}, owner.result));
      }
   }
}

void connection_singleflight_registry::succeed(lease& participant) noexcept {
   if (participant.owner_) {
      complete(*participant.owner_, outcome{.succeeded = true});
   }
}

void connection_singleflight_registry::fail(lease& participant, exceptions::code error, std::string message) noexcept {
   if (participant.owner_) {
      complete(*participant.owner_, outcome{.error = error, .message = std::move(message)});
   }
}

void connection_singleflight_registry::leave(lease& participant) noexcept {
   if (!participant.owner_) {
      return;
   }
   auto owner = std::move(participant.owner_);
   participant.completion_.reset();
   if (owner->participants != 0) {
      --owner->participants;
   }
   const auto found = entries_.find(participant.peer_);
   if (owner->participants == 0 && found != entries_.end() && found->second == owner) {
      entries_.erase(found);
   }
}

void connection_singleflight_registry::close() noexcept {
   for (const auto& [_, value] : entries_) {
      complete(*value, outcome{
                           .error = exceptions::code::closed,
                           .message = "P2P node stopped during connection singleflight",
                       });
   }
   entries_.clear();
}

std::size_t connection_singleflight_registry::size() const noexcept {
   return entries_.size();
}

} // namespace forge::net::p2p::detail
