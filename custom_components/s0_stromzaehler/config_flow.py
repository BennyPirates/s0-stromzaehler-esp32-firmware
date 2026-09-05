"""Config flow for S0 Stromzähler ESP32."""

from __future__ import annotations

import voluptuous as vol

from homeassistant import config_entries
from homeassistant.const import CONF_HOST, CONF_PORT
from homeassistant.data_entry_flow import FlowResult

from .api import S0MeterApiError, S0MeterClient
from .const import DEFAULT_HOST, DEFAULT_PORT, DOMAIN


class S0MeterConfigFlow(config_entries.ConfigFlow, domain=DOMAIN):
    """Handle setup through Settings > Devices & services."""

    VERSION = 1

    async def async_step_user(self, user_input: dict | None = None) -> FlowResult:
        """Handle the initial setup step."""
        errors: dict[str, str] = {}

        if user_input is not None:
            host = user_input[CONF_HOST].strip()
            port = user_input[CONF_PORT]
            await self.async_set_unique_id(f"{host}:{port}")
            self._abort_if_unique_id_configured()

            try:
                await S0MeterClient(self.hass, host, port).async_get_status()
            except S0MeterApiError:
                errors["base"] = "cannot_connect"
            except Exception:  # Do not expose low-level connection errors in the UI.
                errors["base"] = "unknown"
            else:
                return self.async_create_entry(
                    title="S0 Stromzähler ESP32",
                    data={CONF_HOST: host, CONF_PORT: port},
                )

        return self.async_show_form(
            step_id="user",
            data_schema=vol.Schema(
                {
                    vol.Required(CONF_HOST, default=DEFAULT_HOST): str,
                    vol.Required(CONF_PORT, default=DEFAULT_PORT): vol.All(
                        vol.Coerce(int), vol.Range(min=1, max=65535)
                    ),
                }
            ),
            errors=errors,
        )
