#!/usr/bin/env python3

from pathlib import Path
import sys


def require(text: str, needle: str, source: Path) -> None:
    if needle not in text:
        raise SystemExit(f"{source}: missing required typed state surface: {needle}")


def forbid(text: str, needle: str, source: Path) -> None:
    if needle in text:
        raise SystemExit(f"{source}: removed raw state surface returned: {needle}")


def main() -> int:
    root = Path(sys.argv[1]).resolve()
    protocol_path = root / "libraries/chain/protocol/include/forge/chain/protocol/state_query.cppm"
    state_path = root / "libraries/chain/api/include/forge/chain/api/state.cppm"
    exceptions_path = root / "libraries/chain/api/include/forge/chain/api/exceptions.cppm"
    audit_path = root / "libraries/chain/protocol/include/forge/chain/protocol/audit.cppm"
    package_protocol_path = root / "tests/package_chain_protocol_component/main.cpp"
    package_api_path = root / "tests/package_chain_api_component/main.cpp"

    protocol = protocol_path.read_text(encoding="utf-8")
    state = state_path.read_text(encoding="utf-8")
    exceptions = exceptions_path.read_text(encoding="utf-8")
    audit = audit_path.read_text(encoding="utf-8")
    package_protocol = package_protocol_path.read_text(encoding="utf-8")
    package_api = package_api_path.read_text(encoding="utf-8")

    for symbol in (
        "struct state_point_request",
        "struct state_point_response",
        "struct state_range_request",
        "struct state_range_response",
        "struct state_changes_request",
        "struct state_changes_response",
        "struct state_changes_cursor",
        "struct state_mutation",
        "struct state_change_range",
        "struct state_change_batch",
        "struct key_range",
    ):
        forbid(protocol, symbol, protocol_path)

    for method in ("get_point", "get_range", "get_changes"):
        forbid(state, method, state_path)

    for route in (
        '"/v1/chain/state/point"',
        '"/v1/chain/state/range"',
        '"/v1/chain/state/changes"',
    ):
        forbid(state, route, state_path)

    for symbol in (
        "struct table_change_selector",
        "struct table_mutation",
        "struct table_change_batch",
        "struct table_changes_request",
        "struct table_changes_response",
        "struct account_state",
        "struct account_response",
        "struct account_mutation",
        "struct account_change_batch",
        "struct account_changes_request",
        "struct account_changes_response",
    ):
        require(protocol, symbol, protocol_path)

    for symbol in (
        "struct table_selector {",
        "struct table_change {",
        "struct account_change {",
        "using table_selector",
        "using table_change",
        "using account_change",
    ):
        forbid(protocol, symbol, protocol_path)

    require(protocol, "BOOST_DESCRIBE_STRUCT(account_state, (), (creation_date, permissions))", protocol_path)
    require(protocol, "BOOST_DESCRIBE_STRUCT(account_response, (audited_response), (account, state))", protocol_path)
    require(protocol, "BOOST_DESCRIBE_STRUCT(table_changes_response, (audited_response), (blocks, next))", protocol_path)
    require(protocol, "BOOST_DESCRIBE_STRUCT(account_changes_response, (audited_response), (blocks, next))", protocol_path)

    require(state, 'FORGE_API_CONTRACT("forge.chain.api.state", 2, 0)', state_path)
    require(state, "FORGE_API_METHOD_TYPED(get_table_changes", state_path)
    require(state, "FORGE_API_METHOD_TYPED(get_account_changes", state_path)
    require(state, 'FORGE_HTTP_POST(get_table_changes, "/v1/chain/state/table-changes"', state_path)
    require(state, 'FORGE_HTTP_POST(get_account_changes, "/v1/chain/state/account-changes"', state_path)
    require(state, "declare_historical_query<Method>", state_path)
    forbid(state, "declare_state_historical_query<Method>", state_path)
    forbid(state, "history_lost", state_path)
    forbid(exceptions, "history_lost", exceptions_path)
    require(exceptions, "history_unavailable = 15", exceptions_path)
    require(exceptions, "history_unavailable", exceptions_path)

    for value in (
        "state_point = 2",
        "state_range = 3",
        "state_changes = 4",
        "transaction_inclusion = 5",
        "deterministic_composite = 6",
        "unsupported = 7",
    ):
        require(audit, value, audit_path)
    for removed in ("state_projection", "table_changes", "account_changes"):
        forbid(audit, removed, audit_path)

    for package, path in (
        (package_protocol, package_protocol_path),
        (package_api, package_api_path),
    ):
        require(package, "table_change_selector", path)
        require(package, "table_change_batch", path)
        require(package, "account_change_batch", path)
        forbid(package, "protocol::table_selector", path)
        forbid(package, "protocol::table_change ", path)
        forbid(package, "protocol::account_change ", path)
        forbid(package, ".changes =", path)

    require(package_api, "return state_changes;", package_api_path)
    require(package_api, "return state_range;", package_api_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
