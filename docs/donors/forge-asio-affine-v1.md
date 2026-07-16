# Forge Asio Affine Execution Donor Baseline v1

This note records the execution and lifecycle patterns used for
`forge.asio.affine`. The component is a neutral prerequisite for native drivers;
it is not an MDBX wrapper and does not encode database policy.

## Sources Inspected

- Boost.Asio 1.90 `thread_pool`, cancellation state and associated cancellation
  slot implementations used by the current Forge toolchain.
- Existing Forge `asio.compute`, `asio.task` and DB Object single-writer gate.
- libmdbx thread-affinity requirements captured in the separate MDBX design
  documents on the MDBX feature branch.

## Accepted Patterns

- One owned worker thread is the physical affinity boundary.
- A copyable executor submits synchronous operations; native handles remain
  private to the consumer.
- Admission is bounded and FIFO, with distinct pending and waiting budgets.
- Cancellation before execution removes work; cancellation after execution
  starts cannot interrupt an arbitrary native call, so completion wins.
- Shutdown rejects new work, cancels queued work, drains the running operation
  and joins the worker deterministically.
- Completion is signaled back to the executor of the awaiting coroutine.
- Internal waiters suppress automatic `operation_aborted`; their owner converts
  Asio cancellation into typed Forge state transitions before waking them.

## Rejected Patterns

- A strand, because it serializes handlers but does not guarantee one OS thread.
- `asio.compute`, because its pool is intentionally multi-threaded and running
  work uses cooperative cancellation.
- Detached submission handles or fire-and-forget work in v1.
- Unbounded queues and destructor-only lifecycle management.
- Backend-specific names or transaction semantics in the neutral Asio API.

## Verification

Tests prove physical thread identity, FIFO order across both admission queues,
bounded rejection, move-only values, caller-executor continuation,
pre-execution cancellation, completion-wins after start and deterministic
shutdown. DB Object tests prove that replacing its private gate does not change
single-writer, allocator or dropped-transaction behavior.
