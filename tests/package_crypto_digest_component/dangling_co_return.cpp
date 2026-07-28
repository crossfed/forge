#include <coroutine>
#include <cstdint>
#include <span>
#include <string>

import forge.crypto.digest.sha256;

class span_task {
 public:
   struct promise_type {
      span_task get_return_object() {
         return {};
      }
      std::suspend_never initial_suspend() noexcept {
         return {};
      }
      std::suspend_never final_suspend() noexcept {
         return {};
      }
      void return_value(std::span<const std::uint8_t>) {}
      void unhandled_exception() {}
   };
};

span_task dangling() {
   co_return forge::crypto::digest::sha256::hash(std::string{"forge"}).to_uint8_span();
}
