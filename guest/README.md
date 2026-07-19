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
  `"reproducible": false`, report the selected Clang version line and omit an
  unknown source commit. Developer archives bundle the discovered runtime
  dependencies of all shipped tools; they do not retain links to the build
  machine's LLVM installation.

The host tools use the platform C++ ABI of their LLVM and dependency packages:
`libc++` on macOS and `libstdc++` on Linux. Linux archives bundle the selected
`libstdc++` runtime so C++23 library support does not depend on the destination
machine. Contract code is independent of that host choice and always uses the
pinned wasm32 libc++ sysroot shipped by the SDK.

SDK and Forge versions are identical. The sysroot schema and intrinsic
interface have independent versions because either contract can evolve without
changing C++ source compatibility.

## Install A Release Archive

Extract one platform archive and point CMake at its package directory:

```bash
tar -xzf forge-contract-sdk-8.5.0-macos-arm64.tar.gz
export ForgeContract_DIR="$PWD/forge-contract-sdk-8.5.0-macos-arm64/lib/cmake/ForgeContract"
```

The archive includes pinned compiler tools, the wasm32 sysroot with the
prebuilt `libforge_guest_runtime.a`, Forge contract modules, ABI tools,
validation tools, and an example under
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
`apply` entry point. The database C ABI is available through generated
`<eosio/db.h>`, while `<eosio/multi_index.hpp>`, `<eosio/singleton.hpp>` and
`<eosio/fixed_bytes.hpp>` adapt the same C++23 implementation used by modern
contracts. The remaining unchanged EOSIO header corpus grows only through
donor-backed compatibility blocks.

## Persistent Tables

`forge.contract.multi_index` provides primary and up to 16 secondary indexes
without a guest dependency on `forge.db`:

```cpp
#include <forge/contract/serialize.hpp>

import forge.contract.multi_index;
import forge.contract.singleton;

using namespace forge::chain::protocol::literals;

struct [[forge::table("orders")]] order {
   std::uint64_t id = 0;
   std::uint64_t customer = 0;
   std::string memo;

   std::uint64_t primary_key() const { return id; }
   std::uint64_t by_customer() const { return customer; }

   FORGE_SERIALIZE(order, &order::id, &order::customer, &order::memo)
};

using orders = forge::contract::multi_index<
    "orders"_n, order,
    forge::contract::indexed_by<
        "bycustomer"_n,
        forge::contract::const_mem_fun<order, std::uint64_t, &order::by_customer>>>;
using settings = forge::contract::singleton<"settings"_n, std::string>;
```

The API supports primary and secondary lookup, bounds, bidirectional and reverse
iteration, stable loaded-row references, `emplace`, `modify`, `erase`,
`same_payer`, autoincrement and the complete singleton lifecycle. The host owns
the transaction, authorization and RAM policy; a failed action rolls back its
row and index mutations together.

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
C++ and C APIs. The allocator is compiled once into the SDK sysroot; each
contract links the finished archive instead of rebuilding its sources:

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

Interface version 1 contains the exact 148-function union of the pinned CDT and
Spring contract interfaces. Every entry records its C header, WASM signature,
capability set and, where applicable, the protocol feature that enables it.
The seven capability sets are:

```text
core
database
privileged
crypto_ext
bls
call
instant_finality
```

The core set includes the lifecycle, action-data and context functions:

```text
env.eosio_assert
env.eosio_assert_message
env.eosio_assert_code
env.eosio_exit
env.action_data_size
env.read_action_data
env.set_action_return_value
env.current_receiver
```

`read_action_data` follows Spring's legacy contract: it normally returns the
number of bytes copied, but when no bytes are copied it returns the full action
payload size. `action_data_size` remains the direct size query.

The same version includes the 60 Spring/CDT database imports: the ten
primary `db_*_i64` operations and ten operations for each of `idx64`, `idx128`,
`idx256`, `idx_double` and `idx_long_double`. `<forge/contract/intrinsics.h>`
is canonical. The pinned CDT family headers under `<eosio/*.h>` are generated
from that registry without a second declaration list. The shipped interface
defines signatures only. Forge's non-installed test host registers all 148
functions directly from the same registry. It executes the database family
against `forge.db.object`, uses Forge crypto for contract-visible primitives,
and supplies deterministic state for the remaining capability families. This
is an executable SDK oracle, not a product host binding. Authorization policy,
RAM accounting, consensus and the blockchain-owned storage schema remain
responsibilities of the product runtime.

The pinned CDT snapshot contains 14 public EOSIO C headers and 39 public EOSIO
C++ headers. The SDK installs each compatibility header plus the canonical
Forge C headers and 30 modern leaf modules. A committed surface manifest checks
these exact inventories together with attributes, ABI vocabulary and stable
contract-visible errors.

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
LLVM_PREFIX=/path/to/llvm-22.1
cmake -S guest -B build/contract-sdk -G Ninja \
  -DCMAKE_C_COMPILER="$LLVM_PREFIX/bin/clang" \
  -DCMAKE_CXX_COMPILER="$LLVM_PREFIX/bin/clang++" \
  -DCMAKE_PREFIX_PATH="$LLVM_PREFIX" \
  -DFORGE_CONTRACT_PROFILE=developer \
  -DFORGE_CONTRACT_FORGE_DIR="$PWD/build/forge-prefix/lib/cmake/Forge" \
  -DFORGE_CONTRACT_SYSROOT="$PWD/build/wasm32-sysroot"
cmake --build build/contract-sdk -j 4
cmake --build build/contract-sdk --target forge_contract_sdk_archive -j 4
cmake --build build/contract-sdk --target forge_contract_sdk_relocation -j 4
```

`LLVM_PREFIX` identifies the same Clang 22.1 installation used to build the
Forge host package. The guest compiler, dependency scanner, archiver and
ranlib are selected from that LLVM package rather than from an unrelated
program found earlier in `PATH`. `FORGE_CONTRACT_CLANG` remains an explicit
override for controlled developer environments. `wasm-ld` may be supplied by
a separate lld package, as it is in Homebrew.

Developer mode copies `FORGE_CONTRACT_SYSROOT` into a build-owned staged
sysroot before adding `libforge_guest_runtime.a`. The supplied directory is
never modified. The staged archive is included in `sysroot.sha256` and checked
by `ForgeContractConfig.cmake` when a consumer loads the package.

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
- Blockchain controller bindings and deployment are outside this vertical
  block. The executable DB host under `guest/tests/host` is
  intentionally test-only and is never installed or exported.

See `docs/iterations/forge-contract-sdk-toolchain-v1.md` for the accepted
design, donor pins and compatibility scope.
