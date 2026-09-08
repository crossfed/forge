#pragma once

namespace forge::plugins::chain::signer {

class admission_queue;
class admission_cancellation;
struct admission_operation;
struct admission_state;

class admission_lease {
 public:
   admission_lease() = default;
   ~admission_lease();

   admission_lease(const admission_lease&) = delete;
   admission_lease& operator=(const admission_lease&) = delete;
   admission_lease(admission_lease&& other) noexcept;
   admission_lease& operator=(admission_lease&& other) noexcept;

   [[nodiscard]] admission_cancellation bind(boost::asio::cancellation_state inherited);
   [[nodiscard]] boost::asio::cancellation_slot cancellation_slot() noexcept;
   [[nodiscard]] bool cancelled() const noexcept;
   void cancel(boost::asio::cancellation_type type) noexcept;
   void release() noexcept;

 private:
   admission_lease(std::shared_ptr<admission_state> owner, std::shared_ptr<admission_operation> active,
                   std::string caller);

   std::shared_ptr<admission_state> owner_;
   std::shared_ptr<admission_operation> active_;
   std::string caller_;

   friend class admission_queue;
};

class admission_cancellation {
 public:
   ~admission_cancellation();

   admission_cancellation(const admission_cancellation&) = delete;
   admission_cancellation& operator=(const admission_cancellation&) = delete;
   admission_cancellation(admission_cancellation&& other) noexcept;
   admission_cancellation& operator=(admission_cancellation&& other) noexcept;

   [[nodiscard]] boost::asio::cancellation_slot slot() noexcept;

 private:
   admission_cancellation(boost::asio::cancellation_slot parent, std::shared_ptr<admission_operation> active);

   boost::asio::cancellation_slot parent_;
   std::shared_ptr<admission_operation> active_;

   friend class admission_lease;
};

class admission_queue {
 public:
   admission_queue(std::size_t max_inflight, std::size_t max_queued, std::size_t max_queued_bytes,
                   std::size_t max_per_caller);

   admission_queue(const admission_queue&) = delete;
   admission_queue& operator=(const admission_queue&) = delete;

   boost::asio::awaitable<admission_lease> acquire(std::string caller, std::size_t bytes);
   void close() noexcept;
   boost::asio::awaitable<void> wait_for_drain();
   [[nodiscard]] bool closed() const noexcept;

 private:
   std::shared_ptr<admission_state> state_;
};

} // namespace forge::plugins::chain::signer
