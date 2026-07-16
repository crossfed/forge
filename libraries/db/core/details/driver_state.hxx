#pragma once

namespace forge::db::core::detail {

class driver_state final : public std::enable_shared_from_this<driver_state> {
 public:
   enum class close_action : std::uint8_t {
      run,
      already_closed,
   };

   class open_admission {
    public:
      open_admission() = default;
      explicit open_admission(std::shared_ptr<driver_state> owner) noexcept;
      ~open_admission();

      open_admission(const open_admission&) = delete;
      open_admission& operator=(const open_admission&) = delete;
      open_admission(open_admission&& other) noexcept;
      open_admission& operator=(open_admission&& other) noexcept;

      std::shared_ptr<driver_state> publish();

    private:
      void cancel() noexcept;

      std::shared_ptr<driver_state> owner_;
   };

   open_admission admit_open();
   void admit_operation();
   void release_operation() noexcept;
   close_action admit_close();
   void finish_close() noexcept;
   void fail_close() noexcept;
   void release_session() noexcept;

 private:
   enum class phase : std::uint8_t {
      open,
      closing,
      closed,
   };

   void cancel_open() noexcept;
   void publish_open();

   std::mutex mutex_;
   std::size_t opening_ = 0;
   std::size_t active_ = 0;
   std::size_t operations_ = 0;
   phase phase_ = phase::open;
   bool close_running_ = false;

   friend class open_admission;
};

} // namespace forge::db::core::detail
