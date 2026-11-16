"""HTTP Client for the Fritzing C++ MCP Bridge."""

from __future__ import annotations

import math
from typing import Any

import httpx


class BridgeError(RuntimeError):
    """Raised when the Fritzing C++ Bridge returns an error or is unreachable."""

    def __init__(self, message: str, code: str = "BRIDGE_ERROR") -> None:
        super().__init__(message)
        self.code = code


class FritzingBridgeClient:
    """Communicates with the embedded Fritzing C++ MCP HTTP server."""

    def __init__(self, base_url: str = "http://127.0.0.1:8765", timeout: float = 15.0) -> None:
        if not base_url or not base_url.startswith(("http://", "https://")):
            raise ValueError("base_url must start with http:// or https://")
        if not math.isfinite(timeout) or timeout <= 0:
            raise ValueError("timeout must be a positive finite number")
        self.base_url = base_url.rstrip("/")
        self.timeout = timeout

    def health(self) -> dict[str, Any]:
        """Query the /health endpoint of the Fritzing C++ Bridge."""
        try:
            with httpx.Client(timeout=self.timeout) as client:
                resp = client.get(f"{self.base_url}/health")
        except httpx.HTTPError as exc:
            raise BridgeError(f"Fritzing bridge unreachable: {exc}", "CONNECTION_ERROR") from exc
        return self._decode_response(resp)

    def is_online(self) -> bool:
        """Return True if the Fritzing C++ Bridge responds with ok=True."""
        try:
            data = self.health()
            return bool(data.get("ok"))
        except BridgeError:
            return False

    def call(self, method: str, params: dict[str, Any] | None = None) -> dict[str, Any]:
        """Invoke an RPC method on the Fritzing C++ Bridge."""
        clean_method = method.strip() if isinstance(method, str) else ""
        if not clean_method:
            raise ValueError("method must be a non-empty string")
        if params is not None and not isinstance(params, dict):
            raise ValueError("params must be a dict or None")

        payload = {"method": clean_method, "params": params or {}}
        try:
            with httpx.Client(timeout=self.timeout) as client:
                resp = client.post(f"{self.base_url}/rpc", json=payload)
        except httpx.HTTPError as exc:
            raise BridgeError(f"Fritzing bridge request failed: {exc}", "CONNECTION_ERROR") from exc
        return self._decode_response(resp)

    @staticmethod
    def _decode_response(resp: httpx.Response) -> dict[str, Any]:
        """Parse JSON response and raise BridgeError on failure."""
        try:
            data = resp.json()
        except ValueError as exc:
            raise BridgeError(
                f"Invalid JSON from Fritzing bridge (HTTP {resp.status_code})",
                "INVALID_JSON",
            ) from exc

        if not isinstance(data, dict):
            raise BridgeError("Expected JSON object from Fritzing bridge", "INVALID_JSON")

        if resp.status_code >= 400 or not data.get("ok", False):
            err_msg = str(data.get("error") or f"HTTP {resp.status_code}")
            err_code = str(data.get("code") or f"HTTP_{resp.status_code}")
            raise BridgeError(err_msg, err_code)
        return data
