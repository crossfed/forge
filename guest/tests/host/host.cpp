module;

#include <boost/asio/awaitable.hpp>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

module forge.contract.testing.host;

import forge.asio.runtime;
import forge.db.core.driver;
import forge.db.object.store;
import forge.db.object.transaction;
import forge.ids.object_id;
import forge.vm.wasm.backend;

#include "details/host_impl.hxx"

namespace forge::contract::testing {

host::host() : impl_{std::make_unique<impl>()} {}

host::~host() = default;

invocation_result host::invoke(std::span<const std::uint8_t> code, std::uint64_t receiver, std::uint64_t first_receiver,
                               std::uint64_t action, std::vector<std::uint8_t> data) {
   return impl_->invoke(code, receiver, first_receiver, action, std::move(data));
}

std::optional<table> host::find_table(std::uint64_t code, std::uint64_t scope, std::uint64_t table_name) {
   return impl_->find_table(code, scope, table_name);
}

std::optional<key_value> host::find_primary(std::uint64_t code, std::uint64_t scope, std::uint64_t table_name,
                                            std::uint64_t primary) {
   return impl_->find_primary(code, scope, table_name, primary);
}

std::optional<index64> host::find_index64(std::uint64_t code, std::uint64_t scope, std::uint64_t table_name,
                                          std::uint64_t primary) {
   return impl_->find_secondary<index64, index64_index>(code, scope, table_name, primary);
}

std::optional<index128> host::find_index128(std::uint64_t code, std::uint64_t scope, std::uint64_t table_name,
                                            std::uint64_t primary) {
   return impl_->find_secondary<index128, index128_index>(code, scope, table_name, primary);
}

std::optional<index256> host::find_index256(std::uint64_t code, std::uint64_t scope, std::uint64_t table_name,
                                            std::uint64_t primary) {
   return impl_->find_secondary<index256, index256_index>(code, scope, table_name, primary);
}

std::optional<index_double> host::find_index_double(std::uint64_t code, std::uint64_t scope, std::uint64_t table_name,
                                                    std::uint64_t primary) {
   return impl_->find_secondary<index_double, index_double_index>(code, scope, table_name, primary);
}

std::optional<index_long_double> host::find_index_long_double(std::uint64_t code, std::uint64_t scope,
                                                              std::uint64_t table_name, std::uint64_t primary) {
   return impl_->find_secondary<index_long_double, index_long_double_index>(code, scope, table_name, primary);
}

} // namespace forge::contract::testing
