# forge_net_dns

`forge_net_dns` is a product-neutral asynchronous DNS leaf library. It uses
the caller-provided Forge Asio executor and a private pinned c-ares `1.34.8`
backend; it never creates a runtime, an `io_context` or a thread.

## Public modules

- `forge.net.dns.types` provides nameserver injection, query limits and typed
  address/TXT responses.
- `forge.net.dns.exceptions` provides deterministic DNS domain error codes.
- `forge.net.dns.resolver` provides the heavy PIMPL resolver.

`async_resolve_addresses` resolves A, AAAA or both record families and returns
`boost::asio::ip::address` values with TTLs. `async_resolve_txt` returns one
logical TXT resource record per answer; RFC 1035 character-string segments are
concatenated into its owned byte vector. CNAME targets are exposed separately
as bounded canonical names. No public type exposes c-ares headers, values or
encoded DNS wire data.

## Limits and lifecycle

`resolver_options::max_in_flight` bounds registered operations. Per-query
limits bound answers, CNAMEs, one logical record and total returned bytes. The
resolver fails closed with `resource_limit`; it never truncates a response.
An injected IPv6 link-local nameserver must provide `nameserver::scope_id`;
the resolver emits it as c-ares' `[address]:port%scope_id` zone syntax.
For an `address_family::any` query, one usable requested family is sufficient;
an error from the other family is retained only when neither yields an answer.
Malformed or resource-limited records fail the whole operation. If both
families fail without usable answers, error selection is deterministic:
`timeout`, then `temporary_failure`, then `internal`, then `not_found`.

Every high-level resolver operation owns a separate c-ares channel. Its
channel, timer and borrowed socket descriptors live on one resolver strand.
Deadline, stop-token cancellation, `request_cancel()` and `async_close()`
therefore cancel only their own operation channel. `async_close()` is
idempotent and waits until c-ares callbacks plus cancelled timer/descriptor
handlers drain; descriptors are cancelled and released, never closed.

The component intentionally has no `/dnsaddr`, peer discovery, transport or
P2P policy.
