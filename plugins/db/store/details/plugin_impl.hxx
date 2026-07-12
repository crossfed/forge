#pragma once

namespace forge::plugins::db::store {

enum class phase : std::uint8_t {
   registered,
   configured,
   initialized,
   starting,
   ready,
   started,
   stopping,
   stopped,
};

struct managed_store {
   std::string name;
   std::string driver_name;
   std::string path;
   store_options options;
   std::shared_ptr<forge::db::core::driver> driver;
   std::shared_ptr<forge::db::object::store> objects;
   std::shared_ptr<forge::db::blob::store> blobs;
   bool opened = false;
   bool started = false;
};

struct plugin::impl : public std::enable_shared_from_this<plugin::impl> {
   struct pending_open {
      std::string name;
      std::optional<store_config> config;
      store_options options;
      std::shared_ptr<forge::db::core::driver> driver;
      std::shared_ptr<forge::db::object::store> objects;
      std::shared_ptr<forge::db::blob::store> blobs;
   };

   struct opened_store {
      std::shared_ptr<forge::db::core::driver> driver;
      std::shared_ptr<forge::db::object::store> objects;
      std::shared_ptr<forge::db::blob::store> blobs;
   };

   mutable std::mutex mutex;
   config settings;
   std::unordered_map<std::string, std::shared_ptr<managed_store>> stores;
   std::atomic<phase> current = phase::registered;
   bool enabled = true;

   void configure(config value);
   void initialize();
   boost::asio::awaitable<void> open();
   void start();
   void request_stop() noexcept;
   void close();

   void add_store(std::string name,
                  std::shared_ptr<forge::db::core::driver> driver,
                  store_options options);
   [[nodiscard]] std::shared_ptr<managed_store> find_store(const std::string& name) const;
   [[nodiscard]] std::shared_ptr<managed_store> require_store(const std::string& name) const;
   [[nodiscard]] opened_store require_open_store(const std::string& name) const;
   [[nodiscard]] status current_status() const;

 private:
   [[nodiscard]] static std::shared_ptr<forge::db::core::driver> make_configured_driver(const store_config& value);
   void reject_started_setup() const;
   void reject_duplicate_name(const std::string& name) const;
};

} // namespace forge::plugins::db::store
