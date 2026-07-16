# Forge Contract SDK

Forge Contract SDK is the standalone C++23 toolchain for building smart
contracts executed by `forge.vm.wasm`. It compiles contracts for freestanding
`wasm32`, generates Spring-compatible ABI data and a dispatcher, validates the
finished WebAssembly module, and records the complete build identity in a
sidecar manifest.

The SDK is separate from the normal Forge host build. Configuring Forge does
not download LLVM, build a guest sysroot, or compile contract tools.

Reusable host services live in optional `libraries/contract/*` package
components. The command programs in `tools/` are thin adapters over those
libraries; guest code and the SDK assembly remain under `guest/`.

## Profiles

- `release` builds unmodified LLVM `llvmorg-22.1.8`, libc++, libc++abi and
  compiler-rt from commit `ca7933e47d3a3451d81e72ac174dcb5aa28b59d1`.
  Release archives are reproducible and relocatable.
- `developer` uses an installed Clang/lld and a compatible upstream wasm32
  sysroot. It is faster for SDK development, but manifests explicitly contain
  `"reproducible": false`.

SDK and Forge versions are identical. The sysroot schema and intrinsic
interface have independent versions because either contract can evolve without
changing C++ source compatibility.

## Install A Release Archive

Extract one platform archive and point CMake at its package directory:

```bash
tar -xzf forge-contract-sdk-8.5.0-macos-arm64.tar.gz
export ForgeContract_DIR="$PWD/forge-contract-sdk-8.5.0-macos-arm64/lib/cmake/ForgeContract"
```

The archive includes pinned compiler tools, the wasm32 sysroot, Forge contract
modules, ABI tools, validation tools, and an example under
`share/forge-contract/examples/hello`.

## Build A Contract

```cmake
cmake_minimum_required(VERSION 3.31)
project(hello_contract LANGUAGES CXX)

find_package(ForgeContract CONFIG REQUIRED)

forge_add_contract(
   hello
   SOURCES hello.cpp
)
```

For multiple translation units, identify the source that declares the contract
class. Other sources remain ordinary separately compiled implementation files:

```cmake
forge_add_contract(
   token
   SOURCES token_helpers.cpp token.cpp
   DISPATCH_SOURCE token.cpp
)
```

Configure the host project normally. `forge_add_contract` creates an isolated
wasm32 sub-build, so the parent project does not use the SDK toolchain file:

```bash
cmake -S . -B build -G Ninja -DForgeContract_DIR="$ForgeContract_DIR"
cmake --build build -j 4
```

The target always produces:

```text
hello.wasm
hello.abi
hello.contract.json
```

## Modern Contract

```cpp
#include <cstdint>
#include <string>
#include <vector>

import forge.contract;

class [[forge::contract("hello")]] hello : public forge::contract::context {
 public:
   using context::context;

   [[forge::action]] void greet(std::string user, std::vector<std::uint32_t> values) {
      forge::contract::check(!user.empty(), "user must not be empty");
      forge::contract::check(!values.empty(), "values must not be empty");
   }
};
```

`forge::contract::context` provides `get_self()`, `get_first_receiver()` and the
action-data datastream. The generated `apply` dispatcher reads action bytes
through the same target-neutral `forge.raw` codec used by host applications.

## Legacy Source Vocabulary

The initial compatibility veneer accepts the same canonical attributes and
constructor style through EOSIO names:

```cpp
#include <eosio/eosio.hpp>

class [[eosio::contract("hello")]] hello : public eosio::contract {
 public:
   using contract::contract;

   [[eosio::action]] void greet(std::string user);
};

EOSIO_DISPATCH(hello, (greet))
```

`forge` and `eosio` spellings lower to the same Clang annotations, ABI model,
dispatcher, raw codec and intrinsic imports. The veneer is not a second
runtime. `EOSIO_DISPATCH` expands directly to the Forge dispatcher template;
when it is present, `abigen` includes the source without generating a second
`apply` entry point. The database C ABI is already available through generated
`<eosio/db.h>`. The unchanged EOSIO C++ header corpus and `multi_index` belong
to the next compatibility block.

## ABI, Tables And Ricardian Text

`abigen` recognizes action, table and synchronous-call annotations, user
structs, aliases, variants, nested standard containers, action results and
donor-shaped `multi_index`/`singleton` table declarations. ABI 1.2 is emitted
for ordinary contracts; the `calls` extension selects ABI 1.3.

Ricardian source files can be attached without custom commands:

```cmake
forge_add_contract(
   token
   SOURCES token.cpp
   RICARDIAN_CONTRACTS token.contracts.md
   RICARDIAN_CLAUSES token.clauses.md
)
```

Relative Ricardian paths are resolved from the consumer source directory. The
generated ABI tracks contract sources, included headers and Ricardian inputs,
so an incremental build cannot retain stale ABI metadata.

Invalid or ambiguous annotations fail the build. ABI is parsed again by
`contract-check`; it is not accepted merely because JSON syntax is valid.

The tooling suite executes every active ABI pass/fail fixture from pinned CDT
commit `69599db279b7b93d0688502720c15c6962a1401b` as a separate Forge case. When
the donor checkout is present, each generated pass ABI is compared with the CDT
golden after normalizing only its generated comment and historically omitted
empty tail fields. The released SDK does not depend on that checkout.

## Standard Library And Allocation

Contracts use pinned upstream libc++, not Forge-owned replacements for
`std::string`, `std::vector` or algorithms. The shipped profile supports the
core facilities needed by production contracts, including containers,
iterators, algorithms, tuples, optional, variant, span, string views, concepts,
numeric types and utilities.

Dynamic allocation uses the CDT-derived linear-memory allocator behind normal
C++ and C APIs:

```cpp
#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <vector>

auto values = std::vector<std::uint64_t>{};
values.reserve(128);
values.emplace_back(42);

auto text = std::string{"persistent during this action"};
auto* bytes = static_cast<std::byte*>(std::malloc(256));
std::free(bytes);
```

The runtime supplies `malloc`, `free`, `calloc`, `realloc`, aligned allocation,
`new`/`delete`, memory/string primitives and constructor startup. Freed blocks
are reused and coalesced; allocation grows WebAssembly linear memory when
needed. Allocation exhaustion and failed contract checks terminate through the
canonical `env.eosio_assert_message` intrinsic. C++ exceptions and RTTI are
disabled.

The allocator owns only guest linear memory. It provides no host heap, files,
sockets, threads, clocks or random device.

## Intrinsics And Validation

Interface version 1 contains 67 imports. Its seven lifecycle and action-data
functions are:

```text
env.eosio_assert
env.eosio_assert_message
env.eosio_assert_code
env.eosio_exit
env.action_data_size
env.read_action_data
env.set_action_return_value
```

The same version also includes the 60 Spring/CDT database imports: the ten
primary `db_*_i64` operations and ten operations for each of `idx64`, `idx128`,
`idx256`, `idx_double` and `idx_long_double`. `<forge/contract/intrinsics.h>`
is canonical; `<eosio/db.h>` is a thin generated include over it. This block
defines signatures only. Iterator behavior, authorization, RAM accounting,
floating-key checks and storage are supplied by the future blockchain host.

The declarative registry generates guest declarations and the compatibility
manifest consumed by `contract-check`. Validation rejects unknown or
wrongly typed imports, WASI, unsupported WebAssembly features, malformed ABI,
malformed modules and a missing `apply` export. Structural validation uses
`forge.vm.wasm`, so the build and node execution surfaces share one parser and
validation model.

## Build Manifest

`hello.contract.json` is a sidecar and does not alter WebAssembly bytes. It
records:

- SDK and exact LLVM versions;
- sysroot and intrinsic schema versions and hashes;
- release/developer profile and reproducibility status;
- imported host functions and enabled WebAssembly features;
- SHA-256 of the ABI and WebAssembly artifacts.

Keep the manifest beside deployed artifacts. A producer can verify that a
binary was built with an approved interface without trusting a source path or
local compiler installation.

## Build The SDK

Developer mode requires a Forge host package, Clang 22.1 tools and a compatible
upstream wasm32 sysroot:

```bash
cmake -S guest -B build/contract-sdk -G Ninja \
  -DFORGE_CONTRACT_PROFILE=developer \
  -DFORGE_CONTRACT_FORGE_DIR="$PWD/build/forge-prefix/lib/cmake/Forge" \
  -DFORGE_CONTRACT_SYSROOT="$PWD/build/wasm32-sysroot"
cmake --build build/contract-sdk -j 4
cmake --build build/contract-sdk --target forge_contract_sdk_archive -j 4
cmake --build build/contract-sdk --target forge_contract_sdk_relocation -j 4
```

Release mode builds the exact pinned LLVM and guest runtimes from source. Use
`FORGE_CONTRACT_JOBS=4` to bound superbuild parallelism.

## Boundaries

- Target is `wasm32` with the MVP feature profile, not WASI.
- Threads, filesystem, sockets, random device and wall-clock facilities are not
  part of the contract environment.
- C++ exceptions, RTTI and thread-safe statics are disabled.
- `forge.raw` codec sources and guest-safe `forge.chain.protocol` values are
  shared with host code; a second datastream or wire codec is forbidden.
  `FORGE_POLICY_THROW_EXCEPTION` selects typed host exceptions or the canonical
  guest check intrinsic without duplicating those implementations.
- Spring and CDT are pinned compatibility donors and test oracles, never build
  dependencies of a released SDK.
- Blockchain controller bindings, deployment, executable database host
  bindings and `multi_index` are intentionally outside this vertical block.

See `docs/iterations/forge-contract-sdk-toolchain-v1.md` for the accepted
design, donor pins and compatibility scope.
