# Forge Chain Protocol State Projections v1: Local Acceptance Notes

## Status

This note records three reproducible failures observed during the local ARM64
acceptance run for `chain-protocol-state-projections-v1` on 2026-08-26. They are
outside the feature diff and remain separate Forge baseline work. They must not
be silently excluded from a future release or full-suite acceptance run.

The failures were collected at branch commit `50588125`. The final reviewed
feature head `f9bfcadd` changes only:

- `libraries/chain/api/limits.cpp`;
- `tests/chain_api/chain_api_tests.cpp`.

The affected Asio, Config Env and OTLP source and test paths are unchanged from
the branch base, `origin/dev` at `e6674f83`. A separate clean build of
`origin/dev` was not run, so this document does not claim a diagnosed root
cause or an independently reproduced `origin/dev` build failure.

## Environment

- macOS ARM64;
- Homebrew LLVM 22.1.8;
- Ninja;
- `RelWithDebInfo`;
- local test execution with one test process for isolated reproduction.

The task-owned build directory was removed after acceptance. Commands below use
`<build-dir>` as a placeholder for a newly configured equivalent build.

## Observed Failures

| Test | Reproducible symptom | Feature diff overlap | Current disposition |
| --- | --- | --- | --- |
| `test_forge_asio` | Segmentation fault while running `asio_notification_preserves_late_and_racing_wakes` | None in `libraries/asio` or `tests/asio` | Diagnose separately with a focused sanitizer/debugger run |
| `test_forge_config_env` | Expected unsigned-integer diagnostic text is not present | None in `libraries/config/env` or `tests/env` | Establish the actual diagnostic and its canonical wording separately |
| `test_forge_plugins` | OTLP logs exporter startup throws a typed Forge exception with low-value `error=vector` context | None in `plugins/log/otlp` or its test | Diagnose exporter startup and improve failure context separately |

### Asio Notification

Reproduction:

```bash
ctest --test-dir <build-dir> --output-on-failure \
  -R '^test_forge_asio$' -j1

<build-dir>/tests/test_forge_asio --log_level=test_suite
```

The test process consistently reaches
`asio_notification_preserves_late_and_racing_wakes` in
`tests/asio/notification_tests.cpp` and then exits with signal 11 (`139`). The
failure has not been reduced to a specific wake, lifetime or shutdown race.

Follow-up work should isolate this test under ASan and LLDB, preserve the
late/racing-wake semantics and add a regression test for the diagnosed lifetime
or synchronization error. It does not belong in the Chain Protocol/API feature
branch.

### Config Env Diagnostic

Reproduction:

```bash
ctest --test-dir <build-dir> --output-on-failure \
  -R '^test_forge_config_env$' -j1
```

`env_rejects_negative_text_for_unsigned_fields_before_decode` fails at
`tests/env/env_tests.cpp:306`. The test expects the diagnostic to contain
`expected unsigned integer value`, but the substring search returns `npos`.
The same test confirms that the negative value is rejected, the conversion
diagnostic is present at `counter.count`, and a valid value of `42` decodes to
`42U`.

The actual diagnostic text was not printed by the failing assertion. Follow-up
work must first capture it, then decide whether the implementation or the test
has drifted from the canonical typed diagnostic. No wording is inferred here.

### OTLP Exporter Startup

Reproduction:

```bash
<build-dir>/tests/test_forge_plugins \
  --run_test=log_otlp_plugin_test_suite/log_otlp_exports_default_and_named_logger_routes \
  --log_level=test_suite
```

The test fails while starting the OTLP logs exporter at
`plugins/log/otlp/plugin.cpp:112`. The typed Forge exception reports:

```text
failed to start OTLP logs exporter [forge.plugins.log.otlp:2 2] {error=vector}
```

The failing test is declared in `tests/plugins/log_otlp_tests.cpp:211`. The
exporter dependency or environment condition that produces `error=vector` has
not been identified. Follow-up work should preserve the typed exception while
adding actionable, redacted startup context.

## Feature Acceptance Boundary

The complete local CTest run originally reported four failures. The one failure
inside the feature scope, the typed-state static gate, was fixed and rerun. The
remaining three failures are the cases recorded above.

After that correction, the branch-specific run executed 90 tests with these
three known failures excluded: 88 passed and two donor tests were skipped. The
exact Chain Protocol and Chain API package relocation consumers passed, as did
the relevant typed API, HTTP, stream, QUIC, WebSocket, P2P and DB IDs consumers.

This evidence supports the reviewed feature diff; it is not a substitute for a
green unfiltered Forge suite. Before release-wide acceptance, each baseline
failure must either be fixed and covered by a regression test or be explicitly
resolved through a separately reviewed test expectation or environment rule.
