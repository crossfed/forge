module;

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <exception>
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
      const void* address = info->si_addr;
      if (code_memory_range.empty() && memory_range.empty()) {
         siglongjmp(*destination, signal);
      }
      if (address >= memory_range.data() && address < memory_range.data() + memory_range.size()) {
         siglongjmp(*destination, signal);
      }
      if (address >= code_memory_range.data() && address < code_memory_range.data() + code_memory_range.size()) {
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

void setup_signal_handler_impl() {
   struct sigaction action{};
   action.sa_sigaction = &signal_handler;
   sigemptyset(&action.sa_mask);
   sigaddset(&action.sa_mask, SIGPROF);
   action.sa_flags = SA_NODEFER | SA_SIGINFO;
   sigaction(SIGSEGV, &action, &prev_signal_handler<SIGSEGV>);
#ifndef __linux__
   sigaction(SIGBUS, &action, &prev_signal_handler<SIGBUS>);
#endif
   sigaction(SIGFPE, &action, &prev_signal_handler<SIGFPE>);
}

void setup_signal_handler() {
   static const int initialized = (setup_signal_handler_impl(), 0);
   static_cast<void>(initialized);
   static_assert(std::atomic<sigjmp_buf*>::is_always_lock_free,
                 "Atomic pointers must be lock-free to be async signal safe.");
}

} // namespace forge::vm::wasm
