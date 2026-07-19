#pragma once

#include <eosio/name.hpp>

#include <cstdint>

import forge.contract;

namespace eosio {

class contract : public forge::contract::context {
 public:
   using stream_type = context::stream_type;

   enum class exec_type_t : std::uint8_t {
      action,
      call,
      unknown,
   };

   constexpr contract(name self, name first_receiver, stream_type stream) noexcept
       : context(self, first_receiver, stream), _self(self), _first_receiver(first_receiver), _ds(stream) {}

   [[nodiscard]] constexpr name get_self() const noexcept {
      return _self;
   }

   [[nodiscard]] constexpr name get_first_receiver() const noexcept {
      return _first_receiver;
   }

   [[deprecated("use get_first_receiver()")]] [[nodiscard]] constexpr name get_code() const noexcept {
      return get_first_receiver();
   }

   [[nodiscard]] constexpr stream_type& get_datastream() noexcept {
      return _ds;
   }

   [[nodiscard]] constexpr const stream_type& get_datastream() const noexcept {
      return _ds;
   }

   [[nodiscard]] bool is_sync_call() const {
      forge::contract::check(_exec_type != exec_type_t::unknown,
                             "too early to call is_sync_call(). _exec_type has not been set yet");
      return _exec_type == exec_type_t::call;
   }

   constexpr void set_exec_type(exec_type_t type) noexcept {
      _exec_type = type;
   }

 protected:
   name _self;
   name _first_receiver;
   stream_type _ds;
   exec_type_t _exec_type = exec_type_t::unknown;
};

} // namespace eosio

#define CONTRACT class [[eosio::contract]]
#define ACTION [[eosio::action]] void
#define TABLE struct [[eosio::table]]
