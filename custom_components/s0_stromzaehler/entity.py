"""Shared entity support for S0 meter sensors."""

from __future__ import annotations

from homeassistant.const import CONF_HOST, CONF_PORT
from homeassistant.helpers.device_registry import DeviceInfo
from homeassistant.helpers.update_coordinator import CoordinatorEntity

from .const import DOMAIN
from .coordinator import S0MeterCoordinator


class S0MeterEntity(CoordinatorEntity[S0MeterCoordinator]):
    """Base entity attached to the one ESP32 device."""

    _attr_has_entity_name = True

    def __init__(self, coordinator: S0MeterCoordinator, entry_id: str, entry_data: dict) -> None:
        super().__init__(coordinator)
        self._entry_id = entry_id
        self._entry_data = entry_data

    @property
    def device_info(self) -> DeviceInfo:
        """Return the ESP32 device grouping."""
        return DeviceInfo(
            identifiers={(DOMAIN, self._entry_id)},
            name="S0 Stromzähler ESP32",
            manufacturer="Espressif",
            model="ESP32 S0 meter gateway",
            configuration_url=(
                f"http://{self._entry_data[CONF_HOST]}:{self._entry_data[CONF_PORT]}"
            ),
        )
