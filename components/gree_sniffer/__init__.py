import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import CONF_ID

CONF_CHANNEL = "channel"

gree_sniffer_ns = cg.esphome_ns.namespace("gree_sniffer")
GreeSniffer = gree_sniffer_ns.class_("GreeSniffer", cg.Component, uart.UARTDevice)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(GreeSniffer),
            cv.Optional(CONF_CHANNEL, default="unknown"): cv.string_strict,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    cg.add(var.set_channel(config[CONF_CHANNEL]))
