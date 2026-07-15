#pragma once

namespace forge::db::mdbx::detail {

class environment final {
 public:
   static std::shared_ptr<environment> open(const config& value);

   ~environment();

   environment(const environment&) = delete;
   environment& operator=(const environment&) = delete;

   [[nodiscard]] MDBX_env* native() const noexcept;
   [[nodiscard]] MDBX_dbi resolve(const forge::db::core::family& family) const;
   [[nodiscard]] bool open() const noexcept;

   void validate_key(const forge::db::core::record_key& key) const;
   void validate_value(const std::vector<std::byte>& value) const;
   void validate_range(const forge::db::core::record_range& range,
                       const forge::db::core::page_request& request) const;

   void flush(bool sync);
   void close();
   void close_noexcept() noexcept;

 private:
   environment(MDBX_env* native,
               std::map<std::string, MDBX_dbi> families,
               std::size_t max_key_size,
               std::size_t max_value_size);

   MDBX_env* native_ = nullptr;
   std::map<std::string, MDBX_dbi> families_;
   std::size_t max_key_size_ = 0;
   std::size_t max_value_size_ = 0;
};

} // namespace forge::db::mdbx::detail
