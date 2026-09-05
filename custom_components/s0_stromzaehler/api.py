"""HTTP client for the local S0 meter firmware."""

from __future__ import annotations

from typing import Any

from aiohttp import ClientError, ClientResponseError, ClientTimeout

from homeassistant.helpers.aiohttp_client import async_get_clientsession

from .const import REQUEST_TIMEOUT_SECONDS


class S0MeterApiError(Exception):
    """Raised when the ESP32 status endpoint cannot be read."""


class S0MeterClient:
    """Read the read-only status endpoint exposed by the ESP32."""

    def __init__(self, hass, host: str, port: int) -> None:
        self._session = async_get_clientsession(hass)
        self._url = f"http://{host}:{port}/api/status"

    async def async_get_status(self) -> dict[int, dict[str, Any]]:
        """Return the three channels, keyed by their one-based channel number."""
        try:
            async with self._session.get(
                self._url, timeout=ClientTimeout(total=REQUEST_TIMEOUT_SECONDS)
            ) as response:
                response.raise_for_status()
                payload = await response.json(content_type=None)
        except (ClientError, TimeoutError, ValueError) as error:
            raise S0MeterApiError("Unable to read ESP32 status") from error

        try:
            channels = payload["s0_inputs"]["channels"]
            result = {
                int(channel["channel"]): channel
                for channel in channels
                if int(channel["channel"]) in (1, 2, 3)
            }
        except (KeyError, TypeError, ValueError) as error:
            raise S0MeterApiError("ESP32 returned an invalid status payload") from error

        if len(result) != 3:
            raise S0MeterApiError("ESP32 status is missing one or more meter channels")
        return result
