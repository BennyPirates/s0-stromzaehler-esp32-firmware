"""Constants for the S0 Stromzähler ESP32 integration."""

from datetime import timedelta

from homeassistant.const import Platform

DOMAIN = "s0_stromzaehler"
PLATFORMS: tuple[Platform, ...] = (Platform.SENSOR,)

DEFAULT_HOST = "s0-stromzaehler-esp32.local"
DEFAULT_PORT = 80
DEFAULT_SCAN_INTERVAL = timedelta(seconds=10)
REQUEST_TIMEOUT_SECONDS = 10
