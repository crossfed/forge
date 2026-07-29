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

namespace {

using simple_signal_handler = void (*)(int);
using detailed_signal_handler = void (*)(int, siginfo_t*, void*);

enum class forwarded_action : unsigned { default_action, ignore, simple, detailed };

struct signal_handler_state {
   std::atomic<std::uint64_t> generation{0};
   std::atomic<forwarded_action> action{forwarded_action::default_action};
   std::atomic<simple_signal_handler> simple{SIG_DFL};
   std::atomic<detailed_signal_handler> detailed{nullptr};
};

struct signal_handler_snapshot {
   forwarded_action action = forwarded_action::default_action;
   simple_signal_handler simple = SIG_DFL;
   detailed_signal_handler detailed = nullptr;
};

signal_handler_state segv_handler_state;
#ifndef __linux__
signal_handler_state bus_handler_state;
#endif
signal_handler_state fpe_handler_state;
std::mutex signal_handler_mutex;

signal_handler_state& state_for(int signal) noexcept {
   switch (signal) {
   case SIGSEGV:
      return segv_handler_state;
#ifndef __linux__
   case SIGBUS:
      return bus_handler_state;
#endif
   case SIGFPE:
      return fpe_handler_state;
   default:
      std::abort();
   }
}

signal_handler_snapshot previous_handler(int signal) noexcept {
   auto& state = state_for(signal);
   while (true) {
      const auto before = state.generation.load();
      if ((before & 1U) != 0) {
         continue;
      }

      const auto snapshot = signal_handler_snapshot{
          .action = state.action.load(),
          .simple = state.simple.load(),
          .detailed = state.detailed.load(),
      };
      if (state.generation.load() == before) {
         return snapshot;
      }
   }
}

bool is_forge_signal_handler(const struct sigaction& action) noexcept {
   return (action.sa_flags & SA_SIGINFO) != 0 && action.sa_sigaction == &signal_handler;
}

template <int Signal> void install_signal_handler(const struct sigaction& action) {
   auto& state = state_for(Signal);
   const auto generation = state.generation.load();
   // An odd generation keeps signal readers from observing the displaced handler before its fields are published.
   state.generation.store(generation + 1);

   struct sigaction displaced{};
   if (::sigaction(Signal, &action, &displaced) != 0) {
      state.generation.store(generation + 2);
      detail::fail<exceptions::interpreter>("failed to install VM signal handler");
   }
   if (is_forge_signal_handler(displaced)) {
      state.generation.store(generation + 2);
      return;
   }

   if ((displaced.sa_flags & SA_SIGINFO) != 0) {
      state.detailed.store(displaced.sa_sigaction);
      state.action.store(forwarded_action::detailed);
   } else if (displaced.sa_handler == SIG_DFL) {
      state.action.store(forwarded_action::default_action);
   } else if (displaced.sa_handler == SIG_IGN) {
      state.action.store(forwarded_action::ignore);
   } else {
      state.simple.store(displaced.sa_handler);
      state.action.store(forwarded_action::simple);
   }
   state.generation.store(generation + 2);
}

} // namespace

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

   const auto previous = previous_handler(signal);
   switch (previous.action) {
   case forwarded_action::detailed:
      previous.detailed(signal, info, context);
      break;
   case forwarded_action::default_action: {
      struct sigaction action{};
      action.sa_handler = SIG_DFL;
      sigemptyset(&action.sa_mask);
      sigaction(signal, &action, nullptr);
      raise(signal);
      break;
   }
   case forwarded_action::ignore:
      break;
   case forwarded_action::simple:
      previous.simple(signal);
      break;
   }
}

void setup_signal_handler_impl() {
   const auto lock = std::scoped_lock{signal_handler_mutex};
   sigset_t blocked, previous_mask;
   sigemptyset(&blocked);
   sigaddset(&blocked, SIGSEGV);
#ifndef __linux__
   sigaddset(&blocked, SIGBUS);
#endif
   sigaddset(&blocked, SIGFPE);
   detail::check<exceptions::interpreter>(pthread_sigmask(SIG_BLOCK, &blocked, &previous_mask) == 0,
                                          "failed to block VM signals during handler installation");

   struct sigaction action{};
   action.sa_sigaction = &signal_handler;
   sigemptyset(&action.sa_mask);
   sigaddset(&action.sa_mask, SIGPROF);
   action.sa_flags = SA_NODEFER | SA_SIGINFO;
   try {
      install_signal_handler<SIGSEGV>(action);
#ifndef __linux__
      install_signal_handler<SIGBUS>(action);
#endif
      install_signal_handler<SIGFPE>(action);
   } catch (...) {
      static_cast<void>(pthread_sigmask(SIG_SETMASK, &previous_mask, nullptr));
      throw;
   }
   detail::check<exceptions::interpreter>(pthread_sigmask(SIG_SETMASK, &previous_mask, nullptr) == 0,
                                          "failed to restore signal mask after handler installation");
}

void setup_signal_handler() {
   setup_signal_handler_impl();
   static_assert(std::atomic<sigjmp_buf*>::is_always_lock_free,
                 "Atomic pointers must be lock-free to be async signal safe.");
   static_assert(std::atomic<forwarded_action>::is_always_lock_free,
                 "Forwarded signal actions must be lock-free to be async signal safe.");
   static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
                 "Signal handler generations must be lock-free to be async signal safe.");
   static_assert(std::atomic<simple_signal_handler>::is_always_lock_free,
                 "Signal handler pointers must be lock-free to be async signal safe.");
   static_assert(std::atomic<detailed_signal_handler>::is_always_lock_free,
                 "Detailed signal handler pointers must be lock-free to be async signal safe.");
}

} // namespace forge::vm::wasm
