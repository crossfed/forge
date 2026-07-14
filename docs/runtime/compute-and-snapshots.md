# Compute Execution And Snapshot Concurrency

This document records the FORGE concurrency direction for CPU-heavy work and
state-backed services. It defines ownership and correctness boundaries for the
implemented `forge.asio.compute` API.

## Decision

FORGE separates two execution domains:

- `forge::asio::runtime` owns asynchronous I/O, timers, coroutine orchestration
  and lightweight handlers;
- `forge::asio::compute` runs bounded synchronous CPU work
  without occupying runtime workers.

`task::scheduler` remains a multi-worker admission, priority, cancellation and
backpressure mechanism. It is not reduced to one thread and it is not a
substitute for a CPU execution domain. A task priority orders queued work; it
does not preempt a CPU-bound handler that already occupies a runtime worker.

The compute pool is bounded FIFO. Numeric priority remains owned by
`task::scheduler`; it is not duplicated in the compute API. A scheduler task may
await compute work after scheduler admission. Consumers that require strict
capacity isolation need a separately owned pool or a future explicit partition
mechanism, not a priority value that cannot preempt running work.

Components receive these execution facilities from their owner. Plugins and
leaf libraries must not create detached threads or private unbounded pools.

## Public Shape

The public module is `forge.asio.compute`. `compute` is a namespace, avoiding
names such as `compute_pool` and `compute_executor`:

```cpp
namespace forge::asio::compute {

class context;
class executor;
class pool;
template <typename T> class operation;

struct task_options {
   std::string name;
   std::stop_token parent_stop_token;
};

class pool {
 public:
   struct options {
      std::size_t worker_threads = 0;
      std::size_t max_pending_tasks = 1024;
      std::size_t max_waiting_submissions = 1024;
      std::string thread_name = "forge-compute";
   };

   pool();
   explicit pool(options options);
   ~pool();

   [[nodiscard]] compute::executor get_executor() const noexcept;
   [[nodiscard]] metrics snapshot() const;

   void request_stop() noexcept;
   boost::asio::awaitable<void> shutdown();
};

class executor {
 public:
   template <typename Fn>
   auto submit(task_options options, Fn&& work) const;

   template <typename Fn>
   auto try_submit(task_options options, Fn&& work) const;

   template <typename Fn>
   auto execute(task_options options, Fn&& work) const;
};

} // namespace forge::asio::compute
```

The return type is deduced from a synchronous callable accepting either
`compute::context&` or no arguments. `submit()` returns an awaitable operation,
`try_submit()` returns an optional operation, and `execute()` returns an
awaitable result. Callables returning an Asio awaitable are rejected at compile
time.

The owning application keeps `compute::pool`. Libraries, plugins and execution
engines receive only a copyable `compute::executor`, which cannot stop the pool
or manage worker threads. `worker_threads == 0` resolves to at least one
hardware-concurrency worker. Tests and latency-sensitive owners should request
an explicit deterministic count.

## Snapshot And Writer Model

State-backed consumers should combine immutable read snapshots with an ordered
mutation lane:

```text
reader A -> snapshot N -------------------+
reader B -> snapshot N -------------------+ concurrent
writer   -> state N -> commit N+1 --------+
reader C -> snapshot N+1 -----------------+
```

The writer does not invalidate readers that already captured snapshot `N`.
New readers may observe snapshot `N+1` only after the commit is published. A
single-writer policy serializes mutations and commit order; it does not
serialize the whole runtime, task scheduler or read path.

When one physical DB driver exposes multiple logical layers, a coherent read
operation must derive every layer view from the same Core snapshot. Object and
Blob reads opened independently are not proof of one revision. The accepted
shared-read direction is documented in
[`forge-db-state-services-v1.md`](../iterations/forge-db-state-services-v1.md).

Snapshots are operation-scoped or bounded-batch resources. A backend snapshot
must not be held indefinitely because it may retain obsolete storage versions
and delay reclamation.

## Compute Use

The compute executor is intended for bounded synchronous work such as:

- cryptographic verification and hashing;
- compilation, optimization and deterministic virtual-machine execution;
- compression and other CPU-heavy codecs;
- parallel read-only evaluation over immutable snapshots;
- independent preprocessing stages before an ordered mutation.

Offloading does not make one calculation intrinsically faster. It prevents CPU
work from starving I/O and permits independent work to overlap. The caller
suspends while the computation runs and resumes through the asynchronous
contract when a value or exception is available.

State-mutating execution initially remains one deterministic lane. That lane
may run CPU work on the compute executor, but mutation and commit ordering stay
owned by the state/controller layer and its single-writer boundary.

## Parallel Mutable Execution

Snapshots alone do not make concurrent mutations correct. If several
operations execute from snapshot `N`, each must produce a private write overlay
and enough read/write dependency information to validate the result before
commit:

```text
snapshot N
  -> compute A -> overlay A + dependencies
  -> compute B -> overlay B + dependencies
  -> compute C -> overlay C + dependencies
  -> validate in deterministic order
  -> single-writer commit
  -> re-execute conflicted work
```

Overlay ownership, conflict detection, deterministic commit and re-execution
belong to the consuming state machine. They are not hidden inside
`forge::asio::compute`, DB Object or DB Blob. A consumer that does not implement
those rules keeps mutable execution serial while still using parallel
preprocessing and snapshot reads.

## Compute Contract

- running and pending jobs are bounded independently from waiting submitters;
- the Forge FIFO admits at most `worker_threads` jobs into the native Asio pool,
  so Boost.Asio does not become a hidden unbounded queue;
- results and callable exceptions return through `boost::asio::awaitable<T>`;
- cancellation is cooperative through `std::stop_token` and never kills a
  thread;
- completion resumes through the caller's Asio executor;
- metrics expose waiting, pending, running, terminal counts and accumulated
  queue/execution time;
- shutdown rejects new work, cancels pending work, waits for running work and
  joins every worker.

Strict workload isolation is expressed by separately owned pools. Deadlines
remain controller policy implemented with timers and cancellation. Compute does
not embed blockchain, VM, database or product policy.

## Non-Goals

- Replacing `task::scheduler` with a single-thread queue.
- Duplicating scheduler numeric priority inside the compute pool.
- Running blocking CPU work directly on runtime workers.
- Creating a thread pool inside a VM, plugin or database layer.
- Claiming that a thread pool alone provides parallel transaction semantics.
- Moving conflict detection or state commit policy into `forge::asio`.

## Donor Context

The Spring comparison and the accepted/rejected donor patterns are recorded in
[`forge-asio-compute-spring-v1.md`](../donors/forge-asio-compute-spring-v1.md).
