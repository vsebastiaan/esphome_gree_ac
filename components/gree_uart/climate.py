import esphome.config_validation as cv
import esphome.codegen as cg

from esphome.components import climate, uart
from esphome.const import CONF_SUPPORTED_PRESETS
from esphome.components.climate import ClimatePreset

CODEOWNERS = ["@bekmansurov"]
DEPENDENCIES = ["climate", "uart"]

gree_uart_ns = cg.esphome_ns.namespace("gree_uart")
GreeClimate = gree_uart_ns.class_(
    "GreeClimate", climate.Climate, cg.PollingComponent, uart.UARTDevice
)

ALLOWED_CLIMATE_PRESETS = {
    "NONE": ClimatePreset.CLIMATE_PRESET_NONE,
    "BOOST": ClimatePreset.CLIMATE_PRESET_BOOST,
}
validate_presets = cv.enum(ALLOWED_CLIMATE_PRESETS, upper=True)

CONFIG_SCHEMA = (
    climate.climate_schema(GreeClimate)
    .extend(
        {
            cv.Optional(CONF_SUPPORTED_PRESETS): cv.ensure_list(validate_presets),
        }
    )
    .extend(cv.polling_component_schema("10s"))
    .extend(uart.UART_DEVICE_SCHEMA)
)


async def to_code(config):
    var = await climate.new_climate(config)
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    if CONF_SUPPORTED_PRESETS in config:
        cg.add(var.set_supported_presets(config[CONF_SUPPORTED_PRESETS]))
