# Forge chain API Spring v1 donor baseline

## Baseline

- Repository: `https://github.com/AntelopeIO/spring`
- Commit: `e6a99f68b67abc4d89fe716755b2e1394a4991f7`
- License: MIT
- Machine-readable manifest: `tests/chain_api/spring_api_manifest.json`
- Gate: `tests/chain_api/check_spring_api_manifest.py`

The manifest pins the complete HTTP endpoint registrations from:

- `plugins/chain_api_plugin/chain_api_plugin.cpp`
- `plugins/producer_api_plugin/producer_api_plugin.cpp`

It also pins the DTO and behavior source files used by those registrations. The
checker compares the endpoint names as exact sets: 33 chain endpoints and 21
producer endpoints. Counts alone are not acceptance evidence.

## Accepted patterns

- Spring registration, DTO, and behavior ownership are recorded separately.
- Runtime-conditional registrations retain their exact enablement predicate.
- Forge mappings may be direct, server projections, client projections, or the
  local transaction-id utility when the distinction is explicit.
- A donor test or fixture is named when one exists at the pinned commit.
- `donor_evidence.status=none` is allowed only with an explicit reason.
- Spine acceptance is endpoint-specific and uses a stable evidence id.
- One endpoint may name multiple cases from the same source when its contract
  requires distinct lifecycle paths, as for `send_transaction2` finality and
  canonical fork-out retry behavior.

## Rejected patterns

- Inferring completeness from endpoint counts.
- Treating a Forge method declaration as proof that Spine implements it.
- Treating a domain/unit test as endpoint acceptance without exercising the
  endpoint contract.
- Inventing a passing test or replacing missing evidence with an empty response.
- Editing the pinned Spring donor corpus.

## Fail-closed status

All 54 endpoint entries currently have `spine_acceptance.status=present`; no
pending acceptance evidence remains. That declaration is not itself a passing
gate. A pass requires a clean Spine Git checkout from the repository recorded in
the manifest, and the checker reports the exact validated commit. Dirty
worktrees are rejected because uncommitted acceptance evidence is not
reproducible.

The full gate verifies that every referenced source file exists and contains
each exact Boost.Test case:

```sh
python3 tests/chain_api/check_spring_api_manifest.py FORGE_ROOT SPINE_ROOT
```

The manual workflow requires `spine_commit` as a full 40-character commit SHA,
checks out that exact revision, and passes its root to the checker. Omitting the
Spine root is an error rather than a silent downstream skip.

Forge-local CTest uses the explicit manifest-only mode. It validates the
manifest and Forge-local transaction-id fixture, reports Spine acceptance as
`NOT_RUN`, and exits with code 125 so CTest records the gate as skipped rather
than passed:

```sh
python3 tests/chain_api/check_spring_api_manifest.py FORGE_ROOT --manifest-only
```

Scheduled protocol-feature admission is evidenced at the producer owner in
`producer_passes_admin_policy_and_scheduled_features_to_controller_begin`. Its
downstream same-block activation and replay behavior is independently covered
in Spine by
`produced_feature_extension_activates_in_same_block_and_subjective_policy_is_not_replayed`.

The pinned donor has no endpoint-specific `send_transaction2` test or fixture
identified by this audit. Its manifest entry therefore records explicit donor
evidence absence while retaining the registration, DTO, and behavior sources.
