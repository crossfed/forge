#pragma once

namespace forge::net::dns {

// Private, thread-scoped initiation fault used by deterministic lifecycle tests.
class resolver_wait_failure final {
 public:
   enum class point { read, write, timer };

   explicit resolver_wait_failure(point where) noexcept;
   ~resolver_wait_failure();
   resolver_wait_failure(const resolver_wait_failure&) = delete;
   resolver_wait_failure& operator=(const resolver_wait_failure&) = delete;

   [[nodiscard]] bool triggered() const noexcept;
   static void check(point where);

 private:
   point _where;
   bool _triggered = false;
   resolver_wait_failure* _previous;
};

} // namespace forge::net::dns
