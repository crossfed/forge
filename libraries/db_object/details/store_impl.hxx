#pragma once

#include "write_gate.hxx"

namespace forge::db::object {

struct store::impl {
   impl(std::shared_ptr<forge::db::driver> driver_value, store::config config_value, store::options options_value);

   boost::asio::awaitable<forge::db::transaction> open_write_transaction() const;
   boost::asio::awaitable<forge::db::snapshot> open_read_snapshot() const;

   void register_object_type(forge::ids::object_id type, std::type_index model);
   void ensure_registered_type(forge::ids::object_id type, std::type_index model) const;

   std::shared_ptr<forge::db::driver> driver;
   store::config config;
   store::options settings;
   std::shared_ptr<detail::write_gate> write_gate;
   std::map<forge::ids::object_id, std::type_index> registered;
   std::vector<std::shared_ptr<interceptor>> interceptors;
   std::vector<std::shared_ptr<observer>> observers;
};

} // namespace forge::db::object
