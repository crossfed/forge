module;

#include <condition_variable>
#include <mutex>

module forge.vm.wasm.interpret.watchdog;

namespace forge::vm::wasm::interpret {

watchdog::guard::~guard() {
   {
      auto lock = std::unique_lock(_mutex);
      if (_run_state == running && std::chrono::steady_clock::now() < _start + _duration) {
         _run_state = stopped;
      }
   }
   _cond.notify_one();
   _timer.join();
}

void watchdog::guard::runner() {
   auto lock = std::unique_lock(_mutex);
   _cond.wait_until(lock, _start + _duration, [&]() { return _run_state != running; });
   if (_run_state == running) {
      _run_state = interrupted;
      _callback();
   }
}

} // namespace forge::vm::wasm::interpret
