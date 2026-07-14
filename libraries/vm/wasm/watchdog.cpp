module;

#include <condition_variable>
#include <mutex>

module forge.vm.wasm.watchdog;

namespace forge::vm::wasm {

watchdog::guard::~guard() {
   {
      auto lock = std::unique_lock(_mutex);
      _run_state = stopped;
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

} // namespace forge::vm::wasm
