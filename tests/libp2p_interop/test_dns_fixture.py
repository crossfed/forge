import json
import copy
import socket
import struct
import tempfile
import unittest
from pathlib import Path

from dns_fixture import DnsaddrServer
from check_stage6_acceptance import (evidence_contract_for, semantic_fixture, sha256_file,
                                     validate_dnsaddr_evidence, validate_private_dnsaddr_evidence,
                                     validate_successful_raw_record, validate_tcp_yamux_evidence)


def question(name, kind=16):
    labels = b"".join(bytes([len(part)]) + part.encode("ascii") for part in name.split("."))
    return struct.pack("!6H", 123, 0x100, 1, 0, 0, 0) + labels + b"\0" + struct.pack("!HH", kind, 1)


class AuthoritativeDnsTests(unittest.TestCase):
    def test_dns_allows_observed_tls_without_weakening_noise_contract(self):
        result, record, listener = semantic_fixture("dnsaddr")
        result["negotiated_security"] = "/tls/1.0.0"
        self.assertEqual(validate_dnsaddr_evidence(result, record, listener), [])
        self.assertTrue(validate_tcp_yamux_evidence(result, record, listener))
        result["negotiated_security"] = "requested-tls"
        self.assertTrue(validate_dnsaddr_evidence(result, record, listener))

    def test_dns_does_not_substitute_for_identify_evidence(self):
        result, record, listener = semantic_fixture("dnsaddr")
        result.pop("signed_peer_record")
        result.pop("protocol_count")
        self.assertEqual(validate_dnsaddr_evidence(result, record, listener), [])
        self.assertTrue(validate_tcp_yamux_evidence(result, record, listener))
        for field in ("negotiated_transport", "negotiated_security", "negotiated_muxer", "echo_ok", "payload_bytes"):
            changed = dict(result)
            changed.pop(field)
            self.assertTrue(validate_dnsaddr_evidence(changed, record, listener), field)

    def test_evidence_rejects_bypass_wrong_peer_and_missing_hops(self):
        for scenario, validator in (("dnsaddr", validate_dnsaddr_evidence),
                                    ("dnsaddr_private_tcp_yamux_pnet", validate_private_dnsaddr_evidence)):
            result, record, listener = semantic_fixture(scenario)
            self.assertEqual(validator(result, record, listener), [])
            for field in ("dns_input_address", "dns_resolver_configured", "expected_peer_id", "authenticated_remote_peer_id"):
                changed = dict(result)
                changed.pop(field)
                self.assertTrue(validator(changed, record, listener), field)
            for mutate in (lambda value: value["queries"].pop(),
                           lambda value: value["records"].update({"_dnsaddr.root.test": "dnsaddr=/ip4/1.2.3.4/tcp/1"})):
                changed = copy.deepcopy(record)
                mutate(changed["dns_evidence"])
                self.assertTrue(validator(result, changed, listener))

    def test_real_udp_queries_are_required_for_evidence(self):
        with tempfile.TemporaryDirectory() as work:
            path = Path(work) / "dns.json"
            with DnsaddrServer("/ip4/127.0.0.1/tcp/1234/p2p/peer", "peer", path) as server:
                with self.assertRaises(RuntimeError):
                    server.require_chain()
                host, port = server.nameserver.rsplit(":", 1)
                with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as client:
                    client.settimeout(2)
                    for name, value in server.records.items():
                        client.sendto(question(name), (host, int(port)))
                        response = client.recv(4096)
                        self.assertEqual(struct.unpack("!6H", response[:12]), (123, 0x8500, 1, 1, 0, 0))
                        self.assertTrue(response.endswith(value.encode("ascii")))
            server.require_chain()
            self.assertFalse(server._thread.is_alive())
            self.assertEqual(len(json.loads(path.read_text())["queries"]), 2)

    def test_malformed_and_unknown_questions_do_not_create_success(self):
        with tempfile.TemporaryDirectory() as work:
            with DnsaddrServer("/ip4/127.0.0.1/tcp/1/p2p/peer", "peer", Path(work) / "dns.json") as server:
                self.assertIsNone(server._answer(b"short"))
                self.assertIsNone(server._answer(struct.pack("!6H", 1, 0, 1, 0, 0, 0) + b"\xc0\x0c"))
                self.assertIsNone(server._answer(question("bad")[:-1]))
                response = server._answer(question("unknown.test"))
                self.assertEqual(struct.unpack("!6H", response[:12])[1] & 15, 3)
                response = server._answer(question(next(iter(server.records)), kind=1))
                self.assertEqual(struct.unpack("!6H", response[:12])[3], 0)
                with self.assertRaises(RuntimeError):
                    server.require_chain()


class RawDnsRecordTests(unittest.TestCase):
    def setUp(self):
        # Synthetic checker inputs only: no donor process or live interop claim.
        work = tempfile.TemporaryDirectory()
        self.addCleanup(work.cleanup)
        self.work = Path(work.name).resolve()
        self.root = self.work / "artifacts"
        self.root.mkdir()
        self.binaries = {name: self.root / f"{name}-fixture" for name in ("forge", "go")}
        payload, self.record, _ = semantic_fixture("dnsaddr")
        payload.pop("signed_peer_record")
        payload.pop("protocol_count")
        result_file = self.root / "result.json"
        result_file.write_text(json.dumps(payload) + "\n")
        self.dns_file = self.root / "authoritative-dns.json"
        dns = self.record["dns_evidence"]
        self.dns_file.write_text(json.dumps(dns) + "\n")
        dns.update({"log_file": str(self.dns_file), "sha256": sha256_file(self.dns_file)})
        dial_log = self.root / "dial.log"
        listener_log = self.root / "listen.log"
        dial_log.write_text("synthetic dial log\n")
        listener_log.write_text("synthetic listener log\n")
        self.record.update({
            "listener": "go", "profile": "native", "transport_stack": ["tcp", "yamux"],
            "transport": "tcp", "runner_scenario_id": "tcp_stage6/dnsaddr",
            "acceptance_scenario_id": "dnsaddr", "addr": dns["root"],
            "effective_configuration": {
                "activation": "enabled", "profile": "native", "transport_stack": ["tcp", "yamux"],
                "dialer": {"transport": "tcp"}, "listener": {"transport": "tcp"},
            },
            "result": payload | {"result_file": str(result_file), "attempts": [{
                "kind": "dial", "scenario_id": "echo", "exit_code": 0, "log_file": str(dial_log),
                "command": [
                    str(self.binaries["forge"]), "dial", "--scenario", "echo",
                    "--peer-id", self.record["peer_id"], "--addr", dns["root"],
                    "--dns-server", dns["nameserver"], "--result-file", str(result_file),
                    "--store-dir", str(self.root / "dial-store"), "--transport", "tcp",
                ],
            }]},
        })
        self.record["listener_process"].update({
            "log_file": str(listener_log), "terminal_status": {"exit_code": 0, "termination": "graceful"},
            "command": [
                str(self.binaries["go"]), "listen", "--scenario", "echo", "--transport", "tcp",
                "--ready-file", str(self.root / "ready"), "--stop-file", str(self.root / "stop"),
                "--store-dir", str(self.root / "listen-store"), "--features", "ping,identify",
            ],
        })
        self.index = {path: sha256_file(path) for path in (result_file, self.dns_file, dial_log, listener_log)}
        self.assertEqual(self.validate_record(), [])

    def validate_record(self, record=None, used=None):
        return validate_successful_raw_record(
            record=self.record if record is None else record,
            capability_id="addressing.dnsaddr",
            expected_direction="forge_to_go",
            expected_profile="native",
            expected_stack=("tcp", "yamux"),
            expected_runner_scenario="tcp_stage6/dnsaddr",
            expected_acceptance_scenario="dnsaddr",
            expected_evidence_contract=evidence_contract_for("dnsaddr"),
            indexed_evidence=self.index,
            used_evidence=set() if used is None else used,
            binary_paths=self.binaries,
            artifact_root=self.root,
        )

    def test_native_dns_full_raw_record_consumes_authoritative_evidence(self):
        used = set()
        self.assertEqual(self.validate_record(used=used), [])
        self.assertEqual(used, set(self.index))
        self.assertIn(self.dns_file, used)

    def test_raw_record_rejects_tampered_log_even_with_refreshed_hash(self):
        changed = json.loads(self.dns_file.read_text())
        changed["queries"].pop()
        self.dns_file.write_text(json.dumps(changed) + "\n")
        digest = sha256_file(self.dns_file)
        self.record["dns_evidence"]["sha256"] = digest
        self.index[self.dns_file] = digest
        self.assertEqual(self.validate_record(), ["DNSADDR log differs from recorded authoritative observations"])

    def test_raw_record_rejects_tampered_authoritative_hash(self):
        self.record["dns_evidence"]["sha256"] = "0" * 64
        self.assertEqual(self.validate_record(), ["DNSADDR log hash differs from recorded observations"])

    def test_raw_record_rejects_launcher_resolver_and_root_bypasses(self):
        for flag, value in (("--dns-server", "127.0.0.1:4321"),
                            ("--addr", self.record["listener_process"]["listen_addrs"][0])):
            with self.subTest(flag=flag):
                changed = copy.deepcopy(self.record)
                command = changed["result"]["attempts"][0]["command"]
                command[command.index(flag) + 1] = value
                self.assertEqual(self.validate_record(changed),
                                 ["DNSADDR launcher bypasses the authoritative root or resolver"])

    def test_raw_record_rejects_unindexed_authoritative_log(self):
        del self.index[self.dns_file]
        self.assertEqual(self.validate_record(),
                         ["raw runner evidence is absent from the verified evidence index"])

    def test_raw_record_rejects_escaped_authoritative_log_even_when_indexed(self):
        outside = self.work / "outside-dns.json"
        outside.write_bytes(self.dns_file.read_bytes())
        symlink = self.root / "linked-dns.json"
        symlink.symlink_to(outside)
        self.index[outside] = sha256_file(outside)
        for path in (self.root / ".." / outside.name, symlink):
            with self.subTest(path=str(path)):
                changed = copy.deepcopy(self.record)
                changed["dns_evidence"]["log_file"] = str(path)
                self.assertEqual(self.validate_record(changed),
                                 ["DNSADDR authoritative log escapes the artifact directory"])


if __name__ == "__main__":
    unittest.main()
