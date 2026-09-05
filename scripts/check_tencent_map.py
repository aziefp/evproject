#!/usr/bin/env python3
"""Smoke-test the Tencent Location Service without printing the API key."""

from __future__ import annotations

import argparse
import json
import sys
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Any


FROM_COORD = "22.543099,114.057868"
TO_COORD = "22.548456,114.064552"
TIMEOUT_SECONDS = 15


def load_key(path: Path) -> str:
    key = path.read_text(encoding="utf-8").strip()
    if not key or any(character.isspace() for character in key):
        raise ValueError("key file must contain exactly one non-empty key")
    return key


def request_json(endpoint: str, parameters: dict[str, str]) -> dict[str, Any]:
    url = f"{endpoint}?{urllib.parse.urlencode(parameters)}"
    request = urllib.request.Request(url, headers={"User-Agent": "evproject-map-check/1"})
    with urllib.request.urlopen(request, timeout=TIMEOUT_SECONDS) as response:
        return json.loads(response.read().decode("utf-8"))


def check_geocoder(key: str) -> bool:
    payload = request_json(
        "https://apis.map.qq.com/ws/geocoder/v1/",
        {"address": "深圳市民中心", "key": key},
    )
    result = payload.get("result") or {}
    location = result.get("location") or {}
    ok = payload.get("status") == 0
    print(
        "geocoder:",
        f"ok={str(ok).lower()}",
        f"status={payload.get('status')}",
        f"message={payload.get('message')}",
        f"lat={location.get('lat')}",
        f"lng={location.get('lng')}",
    )
    return ok


def check_route(key: str, mode: str) -> bool:
    payload = request_json(
        f"https://apis.map.qq.com/ws/direction/v1/{mode}/",
        {"from": FROM_COORD, "to": TO_COORD, "key": key},
    )
    routes = (payload.get("result") or {}).get("routes") or []
    first = routes[0] if routes else {}
    ok = payload.get("status") == 0 and bool(routes)
    print(
        f"route-{mode}:",
        f"ok={str(ok).lower()}",
        f"status={payload.get('status')}",
        f"message={payload.get('message')}",
        f"routes={len(routes)}",
        f"distance_m={first.get('distance')}",
        f"duration_min={first.get('duration')}",
    )
    return ok


def check_uri(key: str) -> bool:
    parameters = {
        "type": "drive",
        "from": "深圳市民中心",
        "fromcoord": FROM_COORD,
        "to": "莲花山公园",
        "tocoord": TO_COORD,
        "referer": key,
    }
    url = "https://apis.map.qq.com/uri/v1/routeplan?" + urllib.parse.urlencode(parameters)
    request = urllib.request.Request(url, headers={"User-Agent": "evproject-map-check/1"})
    with urllib.request.urlopen(request, timeout=TIMEOUT_SECONDS) as response:
        status = response.status
        content_type = response.headers.get_content_type()
        response.read(256)
    ok = status == 200 and content_type == "text/html"
    print(
        "uri-routeplan:",
        f"ok={str(ok).lower()}",
        f"http={status}",
        f"content_type={content_type}",
    )
    return ok


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--key-file", type=Path, default=Path("key.txt"))
    args = parser.parse_args()

    try:
        key = load_key(args.key_file)
        checks = [
            check_geocoder(key),
            check_route(key, "driving"),
            check_route(key, "walking"),
            check_uri(key),
        ]
    except (OSError, ValueError, json.JSONDecodeError, urllib.error.URLError) as error:
        print(f"map-check failed: {type(error).__name__}: {error}", file=sys.stderr)
        return 2

    return 0 if all(checks) else 1


if __name__ == "__main__":
    raise SystemExit(main())
