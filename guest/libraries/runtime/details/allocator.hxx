#pragma once

#include <cstddef>

namespace forge::contract::runtime {

void* sbrk(std::size_t num_bytes);

} // namespace forge::contract::runtime
