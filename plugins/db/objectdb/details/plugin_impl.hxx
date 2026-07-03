#pragma once

namespace forge::plugins::db::objectdb {

enum class phase : std::uint8_t {
   registered,
   configured,
   initialized,
   started,
   stopping,
   stopped,
};

struct managed_store {
   std::string name;
   std::string driver_name;
   std::string path;
   std::string family;
   forge::objectdb::store::options options;
   std::shared_ptr<forge::objectdb::driver> driver;
   std::shared_ptr<forge::objectdb::store> store;
   bool started = false;
};

struct plugin::impl : public std::enable_shared_from_this<plugin::impl> {
   struct opened_store {
      std::shared_ptr<forge::objectdb::store> store;
      std::shared_ptr<forge::objectdb::driver> driver;
   };

   mutable std::mutex mutex;
   config settings;
   std::unordered_map<std::string, std::shared_ptr<managed_store>> stores;
   std::atomic<phase> current = phase::registered;
   bool enabled = true;

   void configure(config value);
   void initialize();
   void start();
   void request_stop() noexcept;
   void close();

   void add_store(std::string name,
                  std::shared_ptr<forge::objectdb::driver> driver,
                  forge::objectdb::store::options options);
   [[nodiscard]] std::shared_ptr<managed_store> find_store(const std::string& name) const;
   [[nodiscard]] std::shared_ptr<managed_store> require_store(const std::string& name) const;
   [[nodiscard]] opened_store require_open_store(const std::string& name) const;
   [[nodiscard]] status current_status() const;

 private:
   void add_configured_store(const store_config& value);
   void reject_started_setup() const;
   void reject_duplicate_name(const std::string& name) const;
};

class plugin::api_impl final : public api {
 public:
   explicit api_impl(std::shared_ptr<impl> owner);

   boost::asio::awaitable<void>
   add_store(std::string name,
             std::shared_ptr<forge::objectdb::driver> driver,
             forge::objectdb::store::options options) override;
   boost::asio::awaitable<store_handle> store(std::string name) override;
   boost::asio::awaitable<void> flush(std::string name, bool sync) override;
   boost::asio::awaitable<void> flush_all(bool sync) override;
   boost::asio::awaitable<::forge::plugins::db::objectdb::status> status() override;

 private:
   class handle_state;
   std::shared_ptr<impl> owner_;
};

namespace detail {

struct lifecycle {
[[nodiscard]] static std::shared_ptr<plugin::impl> make_impl();
[[nodiscard]] static std::optional<forge::config::component_descriptor> describe_config(const std::shared_ptr<plugin::impl>& impl);
static boost::asio::awaitable<void> configure(const std::shared_ptr<plugin::impl>& impl, forge::config::component_view view);
static boost::asio::awaitable<void> provide(const std::shared_ptr<plugin::impl>& impl, forge::api::provider& provider);
static boost::asio::awaitable<void> initialize(const std::shared_ptr<plugin::impl>& impl, forge::app::plugin_context& context);
static boost::asio::awaitable<void> startup(const std::shared_ptr<plugin::impl>& impl);
static void request_stop(const std::shared_ptr<plugin::impl>& impl) noexcept;
static boost::asio::awaitable<void> shutdown(const std::shared_ptr<plugin::impl>& impl);
};

[[nodiscard]] std::shared_ptr<forge::objectdb::driver> make_configured_driver(const store_config& value);

} // namespace detail

} // namespace forge::plugins::db::objectdb
