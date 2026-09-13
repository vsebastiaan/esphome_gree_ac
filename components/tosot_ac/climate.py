import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate, uart

DEPENDENCIES = ["uart"]

tosot_ac_ns = cg.esphome_ns.namespace("tosot_ac")
TosotAC = tosot_ac_ns.class_("TosotAC", cg.Component, uart.UARTDevice, climate.Climate)

CONFIG_SCHEMA = climate.climate_schema(TosotAC).extend(cv.COMPONENT_SCHEMA).extend(uart.UART_DEVICE_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[cv.GenerateID().schema])
    await climate.register_climate(var, config)
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
