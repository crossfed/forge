# forge_config_program_options

`forge_config_program_options` is the CLI adapter for FORGE config. It uses
Boost.Program_options internally, but returns `forge::config::core::document` and
`forge::schema::diagnostic` so application code never depends on Boost parser
types.

## When To Use

- Build command-line flags from a `config::core::component_registry`.
- Merge CLI values with defaults and YAML/JSON/env config documents.
- Keep application plugins independent from CLI parser implementation.

## When Not To Use

- Do not use this library inside `forge_app` core. `forge_app` consumes config
  documents and descriptors only.
- Do not expose backend parser result maps in public APIs.
- Do not use argv for secrets unless a consuming CLI explicitly accepts the
  risk; prefer stdin or files for secret material.

## Public Module

- `forge.config.program_options`

Target: `forge_config_program_options`.

Dependencies: `forge_config_core`, `forge_schema`, private Boost.Program_options.

## Examples

### Parse CLI Into A Config Document

```cpp
import forge.config.core.key_path;
import forge.config.core.value;
import forge.config.core.document;
import forge.config.core.component;
import forge.config.core.decode;
import forge.config.core.migration;
import forge.config.program_options;

auto registry = forge::config::core::component_registry{};
registry.add(forge::config::core::describe_component<http_config>("http"));

const char* argv[] = {
   "tool",
   "--http.bind-port=9090",
   "--http.tls-enabled=false",
};

auto parsed = forge::config::program_options::parse(3, argv, registry);
if (!parsed.ok()) {
   report_diagnostics(parsed.diagnostics);
} else {
   auto decoded = forge::config::core::decode<http_config>(parsed.document, "http");
   if (!decoded.ok()) {
      report_diagnostics(decoded.diagnostics.entries);
   }
}
```

### Generate Help Text

```cpp
import forge.config.program_options;

auto text = forge::config::program_options::help(registry, "FORGE options");
```

### Merge With File Config

```cpp
import forge.config.core.key_path;
import forge.config.core.value;
import forge.config.core.document;
import forge.config.core.component;
import forge.config.core.decode;
import forge.config.core.migration;

if (!parsed.ok()) {
   report_diagnostics(parsed.diagnostics);
} else {
   auto effective = forge::config::core::merge({
      forge::config::core::defaults_for<http_config>("http"),
      yaml_document,
      dotenv_document,
      process_env_document,
      parsed.document,
   });
}
```

### Flat Root Flags

An empty component section maps fields directly to flag names. This keeps
bootstrap flags such as `--log-level` flat instead of forcing a synthetic
section.

```cpp
auto registry = forge::config::core::component_registry{};
registry.add(forge::config::core::describe_component<daemon_config>(""));

const char* argv[] = {"daemon", "--log-level=debug"};
auto parsed = forge::config::program_options::parse(2, argv, registry);
```

CLI should be the last high-precedence source in a normal daemon bootstrap.
Keep the precedence visible near the program entrypoint so operators can reason
about why a value won.

## Diagnostics

Conversion and parser failures return diagnostics such as
`program_options.convert`; callers can print them through their normal
diagnostic/log pipeline.

```cpp
for (const auto& diagnostic : parsed.diagnostics) {
   std::cerr << diagnostic.path << " [" << diagnostic.code << "] "
             << diagnostic.message << "\n";
}
```

## Risks And Anti-Patterns

- Do not treat CLI parsing as validation success. Decode and inspect diagnostics
  before starting runtime components.
- Do not pass high-value secrets on argv by default; process lists and shell
  history can expose them.
- Do not let plugins own CLI precedence. Plugins publish descriptors; program
  bootstrap composes file/env/CLI documents.

## Typical Mistakes

- Do not let plugins call `parse(argc, argv, ...)` themselves. Plugins publish
  descriptors; the application shell decides which adapters are active.
- Do not encode config source precedence in this library. Use `forge_config_core::merge`.
- Do not document aliases that are not present in schema descriptors.
- Do not parse environment variables here. Use `forge_config_env` for process env and
  `.env` files.
- Do not pass secrets on argv unless the application explicitly accepts the process
  list/history risk. Prefer config files with permissions, stdin or an application
  secret store.
- Do not bridge YAML `options:` arrays into argv. Parse YAML as config and CLI
  as CLI, then merge documents.

## Tests

`test_forge_config_program_options` covers dotted flags, flat root flags, explicit
boolean false, repeated list flags, aliases and conversion errors.
