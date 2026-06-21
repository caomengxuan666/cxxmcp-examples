#!/usr/bin/env python3
"""Run the MCP Inspector CLI against the typed authoring conformance server."""

from __future__ import annotations

import argparse
import json
import shutil
import socket
import subprocess
import sys
import time
from typing import Any


def find_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def wait_for_port(port: int, timeout: float) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
            sock.settimeout(0.25)
            try:
                sock.connect(("127.0.0.1", port))
                return
            except OSError:
                time.sleep(0.1)
    raise RuntimeError(f"server did not listen on 127.0.0.1:{port}")


def decode_json_fragments(text: str) -> list[Any]:
    decoder = json.JSONDecoder()
    values: list[Any] = []
    for index, char in enumerate(text):
        if char not in "[{":
            continue
        try:
            value, _ = decoder.raw_decode(text[index:])
        except json.JSONDecodeError:
            continue
        values.append(value)
    return values


def iter_tool_lists(value: Any) -> list[list[dict[str, Any]]]:
    found: list[list[dict[str, Any]]] = []
    if isinstance(value, dict):
        tools = value.get("tools")
        if isinstance(tools, list) and all(isinstance(item, dict) for item in tools):
            found.append(tools)
        for child in value.values():
            found.extend(iter_tool_lists(child))
    elif isinstance(value, list):
        for child in value:
            found.extend(iter_tool_lists(child))
    return found


def validate_tools_list(stdout: str) -> None:
    tool_lists: list[list[dict[str, Any]]] = []
    for value in decode_json_fragments(stdout):
        tool_lists.extend(iter_tool_lists(value))
    if not tool_lists:
        raise RuntimeError(f"Inspector output did not contain a tools list:\n{stdout}")

    tools = {tool.get("name"): tool for tool in tool_lists[-1]}
    for name in ("echo", "shout_scalar", "shout_object"):
        if name not in tools:
            raise RuntimeError(f"Inspector tools/list output missing {name!r}")

    scalar_schema = tools["shout_scalar"].get("inputSchema", {})
    if scalar_schema.get("type") != "object":
        raise RuntimeError("shout_scalar inputSchema must be an object root")
    scalar_value = scalar_schema.get("properties", {}).get("value", {})
    if scalar_value.get("type") != "string":
        raise RuntimeError("shout_scalar inputSchema.value must be a string")

    object_schema = tools["shout_object"].get("inputSchema", {})
    if object_schema.get("type") != "object":
        raise RuntimeError("shout_object inputSchema must be an object root")
    object_text = object_schema.get("properties", {}).get("text", {})
    if object_text.get("type") != "string":
        raise RuntimeError("shout_object inputSchema.text must be a string")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--server-exe", required=True)
    parser.add_argument("--port", type=int, default=0)
    parser.add_argument(
        "--inspector-package",
        default="@modelcontextprotocol/inspector@0.22.0",
    )
    parser.add_argument("--timeout", type=float, default=45.0)
    args = parser.parse_args()

    npx = shutil.which("npx") or shutil.which("npx.cmd")
    if not npx:
        raise RuntimeError("npx is required to run the MCP Inspector smoke test")

    port = args.port or find_free_port()
    server = subprocess.Popen(
        [args.server_exe, str(port)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    try:
        wait_for_port(port, args.timeout)
        url = f"http://127.0.0.1:{port}/mcp"
        command = [
            npx,
            "-y",
            args.inspector_package,
            "--cli",
            url,
            "--transport",
            "http",
            "--method",
            "tools/list",
        ]
        inspector = subprocess.run(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=args.timeout,
        )
        validate_tools_list(inspector.stdout)
        if inspector.returncode != 0:
            sys.stderr.write(
                "Inspector returned a non-zero exit code after producing a "
                "valid tools/list response. Treating the protocol smoke as "
                "passed; stderr follows:\n"
            )
            sys.stderr.write(inspector.stderr)
        return 0
    finally:
        server.terminate()
        try:
            server.wait(timeout=5)
        except subprocess.TimeoutExpired:
            server.kill()
            server.wait(timeout=5)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # pragma: no cover - command-line diagnostics
        sys.stderr.write(f"{exc}\n")
        raise SystemExit(1)
