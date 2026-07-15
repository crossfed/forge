#pragma once

#include <cstdint>

namespace forge::contract::intrinsic {

#define FORGE_CONTRACT_INTRINSIC(version, identifier, wasm_module, wasm_name, result, parameters)                      \
   extern "C" result identifier parameters __attribute__((import_module(#wasm_module), import_name(#wasm_name)));
#include <forge/contract/intrinsics.def>
#undef FORGE_CONTRACT_INTRINSIC

} // namespace forge::contract::intrinsic
