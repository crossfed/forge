"""Local authoritative DNS for live donor processes, with observed query evidence."""

import json
import secrets
import socket
import struct
import threading
from pathlib import Path


class DnsaddrServer:
    def __init__(self, address: str, peer_id: str, evidence: Path):
        domain = f"{secrets.token_hex(8)}.forge-interop.test"
        self.root = f"/dnsaddr/{domain}/p2p/{peer_id}"
        self.records = {
            f"_dnsaddr.{domain}": f"dnsaddr=/dnsaddr/target.{domain}/p2p/{peer_id}",
            f"_dnsaddr.target.{domain}": f"dnsaddr={address}",
        }
        self.evidence = evidence
        self.queries = []
        self.failure = None
        self._stop = threading.Event()
        self._socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._socket.bind(("127.0.0.1", 0))
        self._socket.settimeout(0.1)
        host, port = self._socket.getsockname()
        self.nameserver = f"{host}:{port}"
        self._thread = threading.Thread(target=self._run, name="interop-dns")

    def __enter__(self):
        self._thread.start()
        return self

    def __exit__(self, *_):
        self._stop.set()
        self._thread.join()
        self._socket.close()
        self.evidence.write_text(json.dumps({
            "nameserver": self.nameserver,
            "root": self.root,
            "records": self.records,
            "queries": self.queries,
        }, indent=2) + "\n")

    def _run(self):
        while not self._stop.is_set():
            try:
                packet, client = self._socket.recvfrom(4096)
            except socket.timeout:
                continue
            if len(self.queries) >= 1024:
                self.failure = "authoritative DNS query budget exhausted"
                return
            response = self._answer(packet)
            if response is not None:
                self._socket.sendto(response, client)

    def _answer(self, packet: bytes):
        if len(packet) < 12:
            return None
        ident, flags, questions, _, _, _ = struct.unpack("!6H", packet[:12])
        if flags & 0xF800 or questions != 1:
            return None
        offset = 12
        labels = []
        while offset < len(packet):
            size = packet[offset]
            offset += 1
            if size == 0:
                break
            if size > 63 or offset + size > len(packet):
                return None
            try:
                labels.append(packet[offset:offset + size].decode("ascii").lower())
            except UnicodeDecodeError:
                return None
            offset += size
        else:
            return None
        if offset + 4 > len(packet) or offset - 12 > 255:
            return None
        kind, record_class = struct.unpack("!HH", packet[offset:offset + 4])
        question = packet[12:offset + 4]
        name = ".".join(labels)
        value = self.records.get(name) if kind == 16 and record_class == 1 else None
        self.queries.append({"name": name, "type": kind, "answered": value is not None})
        # DNS-SD/libp2p reads TXT character strings as one logical record.
        encoded = value.encode("ascii") if value is not None else b""
        data = b"".join(bytes([len(encoded[i:i + 255])]) + encoded[i:i + 255]
                        for i in range(0, len(encoded), 255))
        answer = b"\xc0\x0c" + struct.pack("!HHIH", 16, 1, 30, len(data)) + data if value is not None else b""
        response_flags = 0x8400 | (flags & 0x0100) | (0 if name in self.records else 3)
        return struct.pack("!6H", ident, response_flags, 1, int(value is not None), 0, 0) + question + answer

    def require_chain(self):
        if self.failure is not None:
            raise RuntimeError(self.failure)
        answered = {entry["name"] for entry in self.queries if entry["answered"]}
        if not set(self.records).issubset(answered):
            raise RuntimeError(f"DNSADDR did not traverse the authoritative TXT chain: {self.queries}")
