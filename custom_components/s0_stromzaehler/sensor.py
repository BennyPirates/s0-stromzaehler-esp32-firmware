"""Sensors for S0 Stromzähler ESP32."""

from __future__ import annotations

from dataclasses import dataclass

from homeassistant.components.sensor import (
    SensorDeviceClass,
    SensorEntity,
    SensorEntityDescription,
    SensorStateClass,
)
from homeassistant.config_entries import ConfigEntry
from homeassistant.const import UnitOfEnergy, UnitOfPower
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from .coordinator import S0MeterCoordinator
from .entity import S0MeterEntity


@dataclass(frozen=True, kw_only=True)
class S0SensorDescription(SensorEntityDescription):
    """Describe one sensor reading from the ESP32 JSON payload."""

    key: str
    name: str
    channel: int
    value_key: str
    device_class: SensorDeviceClass
    state_class: SensorStateClass
    unit: str


SENSORS = (
    S0SensorDescription(
        key="wp_power", name="Wärmepumpe Leistung", channel=1, value_key="power_w",
        device_class=SensorDeviceClass.POWER, state_class=SensorStateClass.MEASUREMENT,
        unit=UnitOfPower.WATT,
    ),
    S0SensorDescription(
        key="wp_energy", name="Wärmepumpe Energie", channel=1, value_key="energy_kwh",
        device_class=SensorDeviceClass.ENERGY, state_class=SensorStateClass.TOTAL,
        unit=UnitOfEnergy.KILO_WATT_HOUR,
    ),
    S0SensorDescription(
        key="fewo_power", name="Ferienwohnung Leistung", channel=2, value_key="power_w",
        device_class=SensorDeviceClass.POWER, state_class=SensorStateClass.MEASUREMENT,
        unit=UnitOfPower.WATT,
    ),
    S0SensorDescription(
        key="fewo_energy", name="Ferienwohnung Energie", channel=2, value_key="energy_kwh",
        device_class=SensorDeviceClass.ENERGY, state_class=SensorStateClass.TOTAL,
        unit=UnitOfEnergy.KILO_WATT_HOUR,
    ),
    S0SensorDescription(
        key="haupt_power", name="Hauptwohnung Leistung", channel=3, value_key="power_w",
        device_class=SensorDeviceClass.POWER, state_class=SensorStateClass.MEASUREMENT,
        unit=UnitOfPower.WATT,
    ),
    S0SensorDescription(
        key="haupt_energy", name="Hauptwohnung Energie", channel=3, value_key="energy_kwh",
        device_class=SensorDeviceClass.ENERGY, state_class=SensorStateClass.TOTAL,
        unit=UnitOfEnergy.KILO_WATT_HOUR,
    ),
)


async def async_setup_entry(
    hass: HomeAssistant,
    entry: ConfigEntry,
    async_add_entities: AddEntitiesCallback,
) -> None:
    """Add the six meter entities for one configured ESP32."""
    coordinator: S0MeterCoordinator = entry.runtime_data
    async_add_entities(
        S0MeterSensor(coordinator, entry.entry_id, entry.data, description)
        for description in SENSORS
    )


class S0MeterSensor(S0MeterEntity, SensorEntity):
    """One current-power or accumulated-energy sensor."""

    entity_description: S0SensorDescription

    def __init__(
        self,
        coordinator: S0MeterCoordinator,
        entry_id: str,
        entry_data: dict,
        description: S0SensorDescription,
    ) -> None:
        super().__init__(coordinator, entry_id, entry_data)
        self.entity_description = description
        self._attr_unique_id = f"{entry_id}_{description.key}"
        self._attr_name = description.name
        self._attr_native_unit_of_measurement = description.unit
        self._attr_device_class = description.device_class
        self._attr_state_class = description.state_class

    @property
    def native_value(self) -> int | float | None:
        """Return the current value supplied by the ESP32."""
        value = self.coordinator.data[self.entity_description.channel].get(
            self.entity_description.value_key
        )
        return value if isinstance(value, int | float) else None

    @property
    def extra_state_attributes(self) -> dict[str, int | str | bool | None]:
        """Expose non-primary pulse diagnostics without extra entities."""
        channel = self.coordinator.data[self.entity_description.channel]
        return {
            "gpio": channel.get("gpio"),
            "raw_level": channel.get("raw_level"),
            "active": channel.get("active"),
            "pulses_since_boot": channel.get("pulses"),
        }
