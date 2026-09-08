#!/usr/bin/env python3
"""End-to-end smoke test against a running ev-admin-server."""

from __future__ import annotations

import argparse
import json
import socket
import struct
import time
import uuid


class Client:
    def __init__(self, host: str, port: int) -> None:
        self.sock = socket.create_connection((host, port), timeout=5)
        self.sock.settimeout(10)
        self.token = ""
        self.events: list[dict] = []

    def request(self, message_type: str, data: dict | None = None, request_id: str | None = None) -> dict:
        request_id = request_id or str(uuid.uuid4())
        message = {
            "v": 1,
            "type": message_type,
            "requestId": request_id,
            "timestampMs": int(time.time() * 1000),
            "data": data or {},
        }
        if self.token:
            message["sessionToken"] = self.token
        body = json.dumps(message, ensure_ascii=False, separators=(",", ":")).encode()
        self.sock.sendall(struct.pack(">I", len(body)) + body)
        while True:
            header = self._read_exact(4)
            length = struct.unpack(">I", header)[0]
            response = json.loads(self._read_exact(length))
            if response.get("requestId") == request_id:
                if response.get("ok") is False:
                    raise RuntimeError(response.get("error", {}).get("message", "request failed"))
                return response.get("data", {})
            if response.get("eventId"):
                self.events.append(response)

    def wait_event(self, event_type: str, timeout: float = 5.0) -> dict:
        for index, event in enumerate(self.events):
            if event.get("type") == event_type:
                return self.events.pop(index).get("data", {})
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self.sock.settimeout(max(0.1, deadline - time.monotonic()))
            header = self._read_exact(4)
            length = struct.unpack(">I", header)[0]
            message = json.loads(self._read_exact(length))
            if message.get("type") == event_type and message.get("eventId"):
                return message.get("data", {})
            if message.get("eventId"):
                self.events.append(message)
        raise TimeoutError(f"event not received: {event_type}")

    def _read_exact(self, size: int) -> bytes:
        chunks = bytearray()
        while len(chunks) < size:
            chunk = self.sock.recv(size - len(chunks))
            if not chunk:
                raise ConnectionError("server disconnected")
            chunks.extend(chunk)
        return bytes(chunks)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=45454)
    parser.add_argument("--skip-geocode", action="store_true")
    args = parser.parse_args()

    client = Client(args.host, args.port)
    phone = "139" + str(int(time.time()))[-8:]
    login = client.request("session.loginByPhone", {"phone": phone})
    client.token = login["sessionToken"]
    assert login["profile"]["phone"] == phone

    observer = Client(args.host, args.port)
    observer_login = observer.request("session.loginByPhone", {"phone": phone})
    observer.token = observer_login["sessionToken"]
    recharge_id = str(uuid.uuid4())
    first_balance = client.request("wallet.recharge", {"amountCents": 50000}, recharge_id)["profile"]["balanceCents"]
    second_balance = client.request("wallet.recharge", {"amountCents": 50000}, recharge_id)["profile"]["balanceCents"]
    assert first_balance == second_balance == 50000
    assert observer.wait_event("user.updated")["profile"]["balanceCents"] == 50000
    if not args.skip_geocode:
        located = client.request("location.geocode", {"address": "深圳市民中心"})
        assert located["latitude"] and located["longitude"]

    stations = client.request("station.list", {"latitude": 22.543096, "longitude": 114.057865})["stations"]
    assert len(stations) >= 6
    charger_id = None
    for station in stations:
        detail = client.request("station.get", {"stationId": station["id"]})["station"]
        charger_id = next((c["id"] for c in detail["chargers"] if c["status"] == "idle"), None)
        if charger_id:
            break
    assert charger_id is not None

    order = client.request("order.reserve", {"chargerId": charger_id})["order"]
    order_id = order["id"]
    assert order["status"] == "reserved" and order["targetEnergyWh"] == 20000 and order["progressPercent"] == 0
    assert observer.wait_event("order.updated")["order"]["status"] == "reserved"
    observer.wait_event("stations.changed")
    assert client.request("order.startCharging", {"orderId": order_id})["order"]["status"] == "charging"
    assert observer.wait_event("order.updated")["order"]["status"] == "charging"
    time.sleep(2.2)
    active = client.request("order.getActive")["order"]
    assert active["energyWh"] > 0 and active["amountCents"] > 0
    assert 0 < active["progressPercent"] < 100
    assert client.request("order.stopCharging", {"orderId": order_id})["order"]["status"] == "pending_settlement"
    settled = client.request("order.settle", {"orderId": order_id})
    assert settled["order"]["status"] == "settled"
    assert any(item["id"] == order_id for item in client.request("order.listMine")["orders"])
    assert client.request("order.getActive")["order"] is None
    client.request("session.logout")
    try:
        client.request("user.getProfile")
    except RuntimeError as error:
        assert "请先登录" in str(error)
    else:
        raise AssertionError("logged-out session still accepted")
    client.token = ""
    relogin = client.request("session.loginByPhone", {"phone": phone})
    client.token = relogin["sessionToken"]
    assert relogin["profile"]["phone"] == phone
    coverage = "login/profile/recharge/stations/reserve/start/simulate/stop/settle/history/push-sync/logout/relogin"
    if not args.skip_geocode:
        coverage = coverage.replace("recharge/", "recharge/geocode/")
    print("PASS: " + coverage)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
