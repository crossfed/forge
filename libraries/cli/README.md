# forge_cli

`forge_cli` is the command-line foundation for one-shot Forge clients. It owns
nested command grammar, typed values, validation, contextual help, completion
metadata and asynchronous dispatch. CLI11 is a private parser backend and does
not appear in the public modules.

## When To Use

- Build a one-shot client or developer tool with nested commands.
- Keep reusable command descriptions and coroutine handlers outside `main.cpp`.
- Derive shell completion or other tooling from a typed command descriptor.
- Run one command on Forge Asio with cooperative signal cancellation.

## When Not To Use

- Use `forge_config_program_options` for configuration documents and source
  precedence.
- Use `forge_app` for daemon or plugin lifecycle.
- Use `forge_tui` for full-screen terminal interfaces.
- Do not pass private keys, passwords or bearer tokens through argv. Process
  arguments are observable outside this library.

## Public Modules

- `forge.cli.command`: command records, typed invocation values and the
  machine-readable descriptor.
- `forge.cli.parser`: `dispatch_outcome`, `help_outcome`, `version_outcome`,
  `parse(...)` and contextual `render_help(...)`.
- `forge.cli.runner`: `async_run(...)` plus a blocking one-shot `run(...)`
  adapter backed by `forge_asio`.
- `forge.cli.completion`: deterministic completion candidates derived from an
  `application_descriptor`.
- `forge.cli.terminal`: injectable output/error writers and standard terminal
  detection.
- `forge.cli.exceptions`: typed descriptor, parse, validation, dispatch,
  cancellation and terminal failures.

Target: `forge_cli`. Public dependencies: `forge_asio` and `forge_exceptions`.
CLI11 v2.6.2 is included only while compiling `parser.cpp`; Boost.Charconv is
the private locale-independent floating-point conversion backend.

## Command Example

```cpp
import forge.cli.command;
import forge.cli.runner;

auto app = forge::cli::application{
   .name = "ledger-client",
   .version = "1.0.0",
   .summary = "Ledger client",
   .options = {
      {
         .name = "endpoint",
         .aliases = {"-e"},
         .description = "Remote endpoint",
         .value_name = "URL",
         .required = true,
      },
   },
   .commands = {
      {
         .name = "account",
         .summary = "Account operations",
         .commands = {
            {
               .name = "show",
               .summary = "Show an account",
               .arguments = {{.name = "account", .description = "Account name"}},
               .handler = [](const forge::cli::invocation& input,
                             std::stop_token stop) -> boost::asio::awaitable<int> {
                  if (stop.stop_requested()) {
                     co_return 130;
                  }
                  const auto* account = input.find_argument("account");
                  co_return account == nullptr ? 2 : 0;
               },
            },
         },
      },
   },
};

// main() may return forge::cli::run(app, argc, argv).
```

The canonical option spelling is generated from the record name, so
`name = "endpoint"` defines `--endpoint`; `aliases` adds spellings such as
`-e`. Application options are inherited at every command depth. A command
option is local unless `inherited = true`.

## Validation

CLI11 validates grammar and primitive conversion before Forge creates a typed
`invocation`. Forge then applies required/repeatable rules across inherited
option placements, option conflicts and requirements, typed value validators,
and command validators. The handler is copied into `dispatch_outcome` only
after every validation step succeeds.

`value_validator` and `command_validator` return an optional diagnostic. They
must not perform command side effects because parsing may be used for tests,
completion tooling or preflight checks.

## Completion And Descriptors

`describe(app)` removes handlers and validation callbacks and returns an
`application_descriptor` made only of Forge-owned records. Pass the descriptor
and words through the current cursor to `complete(...)`. Completion candidates
include commands, aliases, visible options and declared `completion_values`.

## Cancellation

`async_run(...)` installs selected `SIGINT`/`SIGTERM` waits on the current Asio
executor and combines them with the optional external `std::stop_token`.
Cancellation is cooperative: handlers must observe the token and ensure their
own awaited operations can finish. The runner never detaches a handler or owns
an unmanaged worker thread.

## Risks And Common Mistakes

- Keep command handlers asynchronous and bounded. A handler that blocks an Asio
  worker also delays signal delivery.
- Do not retain references to `invocation` after the handler returns.
- Do not define the same option name or spelling at multiple active command
  levels; the parser rejects ambiguous descriptors.
- Do not use validation callbacks for authorization. Authorization belongs to
  the downstream operation.
- Do not catch CLI11 exceptions in product code. Only Forge CLI exceptions are
  part of the public contract.

## Tests

`test_forge_cli` covers nested selection, aliases, typed and repeated values,
inherited and local options, conflicts, custom validation, contextual help,
version, completion, runner output and external cancellation. The installed
`cli` package fixture defines and dispatches its own nested command tree.
