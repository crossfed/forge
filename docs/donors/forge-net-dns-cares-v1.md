# forge_net_dns c-ares traceability

## Scope

`forge_net_dns` is a product-neutral leaf DNS client. It resolves only A,
AAAA and TXT records. It deliberately does not define `/dnsaddr`, peer
selection, transport policy or any P2P behaviour.

## Donors inspected

- c-ares 1.34.8: `include/ares.h`, `include/ares_dns_record.h`,
  `src/lib/ares_query.c`, `src/lib/legacy/ares_parse_txt_reply.c`.
- Forge Asio: `libraries/asio/include/forge/asio/runtime.cppm`,
  `libraries/asio/include/forge/asio/notification.cppm`,
  `libraries/asio/notification_impl.cpp`.
- Forge connection lifecycle: `libraries/net/tcp/connector.cpp`.

## Accepted patterns

- Bind each resolver's c-ares channels, socket registry and deadline timers to
  one Forge Asio strand supplied by the caller's existing runtime.
- Use `ARES_OPT_SOCK_STATE_CB`, `ares_process_fds` and `ares_timeout`; Asio
  descriptors borrow c-ares file descriptors and cancel plus release them
  before c-ares destroys the socket.
- Give each query its own c-ares channel so deadline and stop-token
  cancellation cannot cancel another query. Register it before c-ares starts
  the query because a callback can complete synchronously.
- Materialize A and AAAA as Forge-owned typed IP addresses with TTLs. Materialize
  each TXT RR as bounded owned bytes by concatenating its RFC 1035 segments;
  retain bounded CNAME targets separately from requested answers.

## Rejected patterns

- `ARES_OPT_EVENT_THREAD`, c-ares' event thread and any new `io_context` or
  resolver-owned thread.
- Public c-ares headers or `ares_*` types.
- Shared channels for independently cancellable operations.
- DNS-address/P2P interpretation in this leaf component.
