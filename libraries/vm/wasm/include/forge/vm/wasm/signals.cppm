module;

#include <atomic>
#include <cstdlib>
#include <exception>
#include <setjmp.h>
#include <signal.h>
#include <span>
#include <type_traits>
#include <utility>

export module forge.vm.wasm.backend:signals;

import forge.vm.wasm.allocator;
import forge.vm.wasm.debug_info;
import forge.vm.wasm.exceptions;
import forge.vm.wasm.host_function;
import forge.vm.wasm.options;
import forge.vm.wasm.types;
import forge.vm.wasm.watchdog;

namespace forge::vm::wasm {

// Fixes a duplicate symbol build issue when building with `-fvisibility=hidden`
__attribute__((visibility("default"))) extern thread_local std::atomic<sigjmp_buf*> signal_dest;

__attribute__((visibility("default"))) extern thread_local std::span<std::byte> code_memory_range;

__attribute__((visibility("default"))) extern thread_local std::span<std::byte> memory_range;

__attribute__((visibility("default"))) extern thread_local std::atomic<bool> timed_run_has_timed_out;

// Fixes a duplicate symbol build issue when building with `-fvisibility=hidden`
__attribute__((visibility("default"))) extern thread_local std::exception_ptr saved_exception;

template <int Sig> inline struct sigaction prev_signal_handler;

void signal_handler(int signal, siginfo_t* info, void* context);

// only valid inside invoke_with_signal_handler.
// This is a workaround for the fact that it
// is currently unsafe to throw an exception through
// a jit frame.
template <typename F> inline void longjmp_on_exception(F&& f) {
   static_assert(std::is_trivially_destructible_v<std::decay_t<F>>,
                 "longjmp has undefined behavior when it bypasses destructors.");
   bool caught_exception = false;
   try {
      f();
   } catch (...) {
      saved_exception = std::current_exception();
      // Cannot safely longjmp from inside the catch,
      // as that will leak the exception.
      caught_exception = true;
   }
   if (caught_exception) {
      sigset_t block_mask;
      sigemptyset(&block_mask);
      sigaddset(&block_mask, SIGPROF);
      pthread_sigmask(SIG_BLOCK, &block_mask, nullptr);
      sigjmp_buf* dest = std::atomic_load(&signal_dest);
      siglongjmp(*dest, -1);
   }
}

template <typename E> [[noreturn]] inline void throw_(const char* msg) {
   saved_exception = std::make_exception_ptr(E{msg});
   sigset_t block_mask;
   sigemptyset(&block_mask);
   sigaddset(&block_mask, SIGPROF);
   pthread_sigmask(SIG_BLOCK, &block_mask, nullptr);
   sigjmp_buf* dest = std::atomic_load(&signal_dest);
   siglongjmp(*dest, -1);
}

void setup_signal_handler_impl();
void setup_signal_handler();

/// Call a function with a signal handler installed.  If this thread is
/// signalled during the execution of f, the function e will be called with
/// the signal number as an argument.  If f creates any automatic variables
/// with non-trivial destructors, then it must mask the relevant signals
/// during the lifetime of these objects or the behavior is undefined.
///
/// signals handled: SIGSEGV, SIGBUS (except on Linux), SIGFPE
///
// Make this noinline to prevent possible corruption of the caller's local variables.
// It's unlikely, but I'm not sure that it can definitely be ruled out if both
// this and f are inlined and f modifies locals from the caller.
template <typename F, typename E>
[[gnu::noinline]] auto invoke_with_signal_handler(F&& f, E&& e, growable_allocator* code_allocator,
                                                  wasm_allocator* mem_allocator) {
   setup_signal_handler();
   sigjmp_buf dest;
   sigjmp_buf* volatile old_signal_handler = nullptr;
   code_memory_range = code_allocator ? code_allocator->get_code_span() : std::span<std::byte>{};
   memory_range = mem_allocator ? mem_allocator->get_span() : std::span<std::byte>{};
   int sig;
   if ((sig = sigsetjmp(dest, 1)) == 0) {
      // Note: Cannot use RAII, as non-trivial destructors w/ longjmp
      // have undefined behavior. [csetjmp.syn]
      //
      // Warning: The order of operations is critical here.
      // We also have to register signal_dest before unblocking
      // signals to make sure that only our signal handler is executed
      // if the caller has previously blocked signals.
      old_signal_handler = std::atomic_exchange(&signal_dest, &dest);
      sigset_t unblock_mask, old_sigmask; // Might not be preserved across longjmp
      sigemptyset(&unblock_mask);
      sigaddset(&unblock_mask, SIGSEGV);
      sigaddset(&unblock_mask, SIGBUS);
      sigaddset(&unblock_mask, SIGFPE);
      sigaddset(&unblock_mask, SIGPROF);
      pthread_sigmask(SIG_UNBLOCK, &unblock_mask, &old_sigmask);
      try {
         f();
         pthread_sigmask(SIG_SETMASK, &old_sigmask, nullptr);
         std::atomic_store(&signal_dest, old_signal_handler);
      } catch (...) {
         pthread_sigmask(SIG_SETMASK, &old_sigmask, nullptr);
         std::atomic_store(&signal_dest, old_signal_handler);
         throw;
      }
   } else {
      std::atomic_store(&signal_dest, old_signal_handler);
      if (sig == -1) {
         std::exception_ptr exception = std::move(saved_exception);
         saved_exception = nullptr;
         std::rethrow_exception(exception);
      } else {
         e(sig);
      }
   }
}

template <typename F, typename E>
auto invoke_with_signal_handler(F&& f, E&& e, growable_allocator& code_allocator, wasm_allocator* mem_allocator) {
   return invoke_with_signal_handler(std::forward<F>(f), std::forward<E>(e), &code_allocator, mem_allocator);
}

} // namespace forge::vm::wasm
