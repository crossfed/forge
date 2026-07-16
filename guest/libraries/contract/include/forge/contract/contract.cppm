module;

#include <cstdint>

export module forge.contract;

export import forge.chain.protocol.values;
export import forge.contract.intrinsics;
export import forge.raw.stream;

export namespace forge::contract {

class base {
 public:
   using stream_type = forge::datastream<const char*>;

   constexpr base(chain::protocol::name self, chain::protocol::name first_receiver, stream_type stream) noexcept
       : self_(self), first_receiver_(first_receiver), stream_(stream) {}

   [[nodiscard]] constexpr chain::protocol::name get_self() const noexcept {
      return self_;
   }

   [[nodiscard]] constexpr chain::protocol::name get_first_receiver() const noexcept {
      return first_receiver_;
   }

   [[deprecated("use get_first_receiver()")]] [[nodiscard]] constexpr chain::protocol::name get_code() const noexcept {
      return first_receiver_;
   }

   [[nodiscard]] constexpr stream_type& get_datastream() noexcept {
      return stream_;
   }

   [[nodiscard]] constexpr const stream_type& get_datastream() const noexcept {
      return stream_;
   }

 protected:
   chain::protocol::name self_;
   chain::protocol::name first_receiver_;
   stream_type stream_;
};

} // namespace forge::contract
