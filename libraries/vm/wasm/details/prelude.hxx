#pragma once

#include <forge/exceptions/macros.hpp>
#include <forge/vm/wasm/host_function.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <setjmp.h>
#include <signal.h>
#include <softfloat.hpp>
#include <span>
#include <string>
#include <string_view>
#include <sys/mman.h>
#include <sys/types.h>
#include <system_error>
#include <thread>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unistd.h>
#include <utility>
#include <variant>
#include <vector>

#include <cxxabi.h>

#if defined(__x86_64__)
#include <cpuid.h>
#endif

#ifndef FORGE_VM_WASM_EXPORT
#define FORGE_VM_WASM_EXPORT
#endif

#define FORGE_VM_WASM_ASSERT(expression, exception_type, message)                                                     \
   FORGE_MULTILINE_MACRO_BEGIN                                                                                       \
   if (UNLIKELY(!(expression))) {                                                                                    \
      FORGE_THROW_EXCEPTION(exception_type, message);                                                                \
   }                                                                                                                 \
   FORGE_MULTILINE_MACRO_END
