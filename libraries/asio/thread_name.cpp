#include "details/thread_name.hxx"

#if defined(__APPLE__) || defined(__linux__)
#include <pthread.h>
#endif

namespace forge::asio::detail {

void set_current_thread_name(const std::string& name) noexcept {
   if (name.empty()) {
      return;
   }
#if defined(__APPLE__)
   static_cast<void>(pthread_setname_np(name.c_str()));
#elif defined(__linux__)
   auto limited = name.substr(0, 15);
   static_cast<void>(pthread_setname_np(pthread_self(), limited.c_str()));
#else
   static_cast<void>(name);
#endif
}

} // namespace forge::asio::detail
