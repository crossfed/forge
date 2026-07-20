module;

#include <compare>
#include <cstdint>

export module forge.contract.compatibility_asset;

import forge.chain.protocol.values;
import forge.contract.compatibility_name;
import forge.raw.codec;

export namespace forge::contract::compatibility {

struct asset {
   static constexpr std::int64_t max_amount = chain::protocol::asset::max_amount;

   std::int64_t amount = 0;
   chain::protocol::symbol symbol{};

   constexpr asset(std::int64_t raw_amount = 0) : amount(raw_amount) {}

   asset(std::int64_t raw_amount, chain::protocol::symbol raw_symbol)
       : asset(chain::protocol::asset{raw_amount, raw_symbol}) {}

   constexpr asset(const chain::protocol::asset& value) : amount(value.amount), symbol(value.sym) {}

   [[nodiscard]] constexpr bool is_amount_within_range() const noexcept {
      return -max_amount <= amount && amount <= max_amount;
   }

   [[nodiscard]] constexpr bool is_valid() const noexcept {
      return is_amount_within_range() && symbol.is_valid();
   }

   void set_amount(std::int64_t value) {
      auto converted = protocol_value();
      converted.set_amount(value);
      assign(converted);
   }

   asset operator-() const {
      return {-protocol_value()};
   }

   asset& operator+=(const asset& value) {
      auto converted = protocol_value();
      converted += value.protocol_value();
      assign(converted);
      return *this;
   }

   asset& operator-=(const asset& value) {
      auto converted = protocol_value();
      converted -= value.protocol_value();
      assign(converted);
      return *this;
   }

   asset& operator*=(std::int64_t value) {
      auto converted = protocol_value();
      converted *= value;
      assign(converted);
      return *this;
   }

   asset& operator/=(std::int64_t value) {
      auto converted = protocol_value();
      converted /= value;
      assign(converted);
      return *this;
   }

   friend asset operator+(asset left, const asset& right) {
      return left += right;
   }

   friend asset operator-(asset left, const asset& right) {
      return left -= right;
   }

   friend asset operator*(asset value, std::int64_t multiplier) {
      return value *= multiplier;
   }

   friend asset operator*(std::int64_t multiplier, asset value) {
      return value *= multiplier;
   }

   friend asset operator/(asset value, std::int64_t divisor) {
      return value /= divisor;
   }

   friend std::int64_t operator/(const asset& left, const asset& right) {
      return left.protocol_value() / right.protocol_value();
   }

   friend bool operator==(const asset& left, const asset& right) {
      return left.protocol_value() == right.protocol_value();
   }

   friend std::strong_ordering operator<=>(const asset& left, const asset& right) {
      return left.protocol_value() <=> right.protocol_value();
   }

   template <typename Stream> friend void raw_pack(Stream& stream, const asset& value) {
      chain::protocol::raw_pack(stream, value.protocol_value());
   }

   template <typename Stream> friend void raw_unpack(Stream& stream, asset& value) {
      auto converted = chain::protocol::asset{};
      chain::protocol::raw_unpack(stream, converted);
      value.assign(converted);
   }

 private:
   friend struct extended_asset;

   [[nodiscard]] constexpr chain::protocol::asset protocol_value() const noexcept {
      auto value = chain::protocol::asset{};
      value.amount = amount;
      value.sym = symbol;
      return value;
   }

   constexpr void assign(const chain::protocol::asset& value) noexcept {
      amount = value.amount;
      symbol = value.sym;
   }
};

struct extended_asset {
   asset quantity{};
   name contract{};

   constexpr extended_asset() = default;

   extended_asset(std::int64_t amount, chain::protocol::extended_symbol symbol)
       : extended_asset(chain::protocol::extended_asset{amount, symbol}) {}

   constexpr extended_asset(asset value, name raw_contract) : quantity(value), contract(raw_contract) {}

   constexpr extended_asset(const chain::protocol::extended_asset& value)
       : quantity(value.quantity), contract(value.contract) {}

   [[nodiscard]] constexpr chain::protocol::extended_symbol get_extended_symbol() const noexcept {
      return {quantity.symbol, contract};
   }

   extended_asset operator-() const {
      return {-protocol_value()};
   }

   extended_asset& operator+=(const extended_asset& value) {
      auto converted = protocol_value();
      converted += value.protocol_value();
      assign(converted);
      return *this;
   }

   extended_asset& operator-=(const extended_asset& value) {
      auto converted = protocol_value();
      converted -= value.protocol_value();
      assign(converted);
      return *this;
   }

   extended_asset& operator*=(std::int64_t value) {
      auto converted = protocol_value();
      converted *= value;
      assign(converted);
      return *this;
   }

   extended_asset& operator/=(std::int64_t value) {
      auto converted = protocol_value();
      converted /= value;
      assign(converted);
      return *this;
   }

   friend extended_asset operator+(extended_asset left, const extended_asset& right) {
      return left += right;
   }

   friend extended_asset operator-(extended_asset left, const extended_asset& right) {
      return left -= right;
   }

   friend extended_asset operator*(extended_asset value, std::int64_t multiplier) {
      return value *= multiplier;
   }

   friend extended_asset operator*(std::int64_t multiplier, extended_asset value) {
      return value *= multiplier;
   }

   friend extended_asset operator/(extended_asset value, std::int64_t divisor) {
      return value /= divisor;
   }

   friend std::int64_t operator/(const extended_asset& left, const extended_asset& right) {
      if (left.contract != right.contract) {
         chain::protocol::detail::fail_value("type mismatch");
      }
      return left.quantity / right.quantity;
   }

   friend bool operator==(const extended_asset& left, const extended_asset& right) {
      return left.protocol_value() == right.protocol_value();
   }

   friend std::strong_ordering operator<=>(const extended_asset& left, const extended_asset& right) {
      return left.protocol_value() <=> right.protocol_value();
   }

   template <typename Stream> friend void raw_pack(Stream& stream, const extended_asset& value) {
      chain::protocol::raw_pack(stream, value.protocol_value());
   }

   template <typename Stream> friend void raw_unpack(Stream& stream, extended_asset& value) {
      auto converted = chain::protocol::extended_asset{};
      chain::protocol::raw_unpack(stream, converted);
      value.assign(converted);
   }

 private:
   [[nodiscard]] constexpr chain::protocol::extended_asset protocol_value() const noexcept {
      return {quantity.protocol_value(), static_cast<const name::base&>(contract)};
   }

   constexpr void assign(const chain::protocol::extended_asset& value) noexcept {
      quantity = asset{value.quantity};
      contract = name{value.contract};
   }
};

} // namespace forge::contract::compatibility
