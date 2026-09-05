"""Coordinator for S0 meter status updates."""

from __future__ import annotations

import logging
from typing import Any

from homeassistant.core import HomeAssistant
from homeassistant.helpers.update_coordinator import DataUpdateCoordinator, UpdateFailed

from .api import S0MeterApiError, S0MeterClient
from .const import DEFAULT_SCAN_INTERVAL, DOMAIN

_LOGGER = logging.getLogger(__name__)


class S0MeterCoordinator(DataUpdateCoordinator[dict[int, dict[str, Any]]]):
    """Fetch all meter channels together from one ESP32 request."""

    def __init__(self, hass: HomeAssistant, client: S0MeterClient) -> None:
        super().__init__(
            hass,
            logger=_LOGGER,
            name=DOMAIN,
            update_interval=DEFAULT_SCAN_INTERVAL,
            always_update=False,
        )
        self.client = client

    async def _async_update_data(self) -> dict[int, dict[str, Any]]:
        try:
            return await self.client.async_get_status()
        except S0MeterApiError as error:
            raise UpdateFailed(str(error)) from error
