module;

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <mutex>
#include <setjmp.h>
#include <signal.h>
#include <span>

module forge.vm.wasm.backend;

import :signals;

namespace forge::vm::wasm {

__attribute__((visibility("default"))) thread_local std::atomic<sigjmp_buf*> signal_dest{nullptr};
__attribute__((visibility("default"))) thread_local std::span<std::byte> code_memory_range;
__attribute__((visibility("default"))) thread_local std::span<std::byte> memory_range;
__attribute__((visibility("default"))) thread_local std::atomic<bool> timed_run_has_timed_out{false};
__attribute__((visibility("default"))) thread_local std::exception_ptr saved_exception{nullptr};

void signal_handler(int signal, siginfo_t* info, void* context) {
   auto* destination = std::atomic_load(&signal_dest);
   if (destination) {
      const auto address = reinterpret_cast<std::uintptr_t>(info->si_addr);
      if (code_memory_range.empty() && memory_range.empty()) {
         siglongjmp(*destination, signal);
      }
      if (detail::contains_address(memory_range, address)) {
         siglongjmp(*destination, signal);
      }
      if (detail::contains_address(code_memory_range, address)) {
         if ((signal == SIGSEGV || signal == SIGBUS) && !timed_run_has_timed_out.load(std::memory_order_acquire)) {
            return;
         }
         siglongjmp(*destination, signal);
      }
   }

   struct sigaction* previous = nullptr;
   switch (signal) {
   case SIGSEGV:
      previous = &prev_signal_handler<SIGSEGV>;
      break;
   case SIGBUS:
      previous = &prev_signal_handler<SIGBUS>;
      break;
   case SIGFPE:
      previous = &prev_signal_handler<SIGFPE>;
      break;
   default:
      std::abort();
   }

   if (previous->sa_flags & SA_SIGINFO) {
      previous->sa_sigaction(signal, info, context);
   } else if (previous->sa_handler == SIG_DFL) {
      sigaction(signal, previous, nullptr);
      raise(signal);
   } else if (previous->sa_handler != SIG_IGN) {
      previous->sa_handler(signal);
   }
}

namespace {

std::mutex signal_handler_mutex;

bool is_forge_signal_handler(const struct sigaction& action) noexcept {
   return (action.sa_flags & SA_SIGINFO) != 0 && action.sa_sigaction == &signal_handler;
}

template <int Signal> void ensure_signal_handler(const struct sigaction& action) {
   struct sigaction current{};
   detail::check<exceptions::interpreter>(::sigaction(Signal, nullptr, &current) == 0,
                                          "failed to inspect VM signal handler");
   if (is_forge_signal_handler(current)) {
      return;
   }

   prev_signal_handler<Signal> = current;
   detail::check<exceptions::interpreter>(::sigaction(Signal, &action, nullptr) == 0,
                                          "failed to install VM signal handler");
}

} // namespace

void setup_signal_handler_impl() {
   const auto lock = std::scoped_lock{signal_handler_mutex};
   struct sigaction action{};
   action.sa_sigaction = &signal_handler;
   sigemptyset(&action.sa_mask);
   sigaddset(&action.sa_mask, SIGPROF);
   action.sa_flags = SA_NODEFER | SA_SIGINFO;
   ensure_signal_handler<SIGSEGV>(action);
#ifndef __linux__
   ensure_signal_handler<SIGBUS>(action);
#endif
   ensure_signal_handler<SIGFPE>(action);
}

void setup_signal_handler() {
   setup_signal_handler_impl();
   static_assert(std::atomic<sigjmp_buf*>::is_always_lock_free,
                 "Atomic pointers must be lock-free to be async signal safe.");
}

} // namespace forge::vm::wasm
