# Donor Traceability: Forge Application Connect Services v1

## Purpose

Forge needs an application composition phase for clients that can be created
only after plugin initialization and must be available before plugin startup.
The design must not turn `forge_app` into a transport owner or a general-purpose
dependency injection framework.

## Donors

### Existing Forge Application Lifecycle

Inspected Forge surfaces:

- `libraries/app/include/forge/app/application_builder.cppm`;
- `libraries/app/include/forge/app/application_shell.cppm`;
- `libraries/app/application_builder.cpp`;
- `libraries/app/application_shell.cpp`;
- `libraries/app/application.cpp`;
- `tests/app/app_tests.cpp`.

Accepted:

- one shell-owned lifecycle;
- app and plugin API publication before plugin initialization;
- deterministic plugin ordering and reverse shutdown;
- rollback on asynchronous initialization failure;
- `application_builder` as the normal composition root.

Rejected:

- a second builder-owned lifecycle;
- late mutation through the existing read-only `after_initialize` callback;
- using events as service discovery;
- a plugin with no behavior beyond registration of one application client.

### .NET Generic Host

The [.NET Generic Host](https://learn.microsoft.com/en-us/dotnet/core/extensions/generic-host)
is the donor for an application composition root that prepares shared services
before hosted work starts.

Accepted:

- application-owned registration before hosted service startup;
- host-owned lifetime and shutdown;
- consumers receive an already constructed long-lived service.

Rejected:

- constructor auto-wiring;
- scoped and transient service lifetimes;
- reflection-driven activation;
- treating Forge plugins as entries in a generic DI container.

### gRPC Channel

The [gRPC C++ Channel](https://grpc.github.io/grpc/cpp/classgrpc_1_1_channel.html)
is the donor for publishing one stable client object while connection state
changes internally.

Accepted:

- a channel/client object represents an endpoint independently of its current
  connectivity state;
- reconnect and connectivity transitions remain inside the transport/client;
- callers retain a stable typed object rather than rebuilding composition on
  every connection change.

Rejected:

- exposing transport state as application lifecycle state;
- replacing a published service whenever a connection reconnects;
- performing resolver lookup in each product hot-path request.

## Forge Target

The resulting Forge surface is deliberately narrow:

- `application_builder::connect(...)`;
- a mutable `connect_context` limited to the connect phase;
- exact-type one-time service publication;
- read-only service lookup for application and plugin startup.

The API registry continues to own typed callable API contracts. The connected
service registry owns process-local client objects. Neither registry performs
constructor injection or imports concrete transports.

## Proof

Proof belongs primarily in `test_forge_app`:

- exact lifecycle order;
- callback ordering;
- plugin API consumption during connect;
- service visibility before startup;
- immutable publication boundary;
- typed duplicate, missing and closed-registry failures;
- rollback and destruction order;
- static dependency check proving `forge_app` does not import Chain or P2P.
