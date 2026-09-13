import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import CONF_ID

CONF_INTERVAL = "interval"

gree_replay_ns = cg.esphome_ns.namespace("gree_replay")
GreeReplay = gree_replay_ns.class_("GreeReplay", cg.Component, uart.UARTDevice)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(GreeReplay),
            cv.Optional(CONF_INTERVAL, default="300ms"): cv.positive_time_period_milliseconds,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    cg.add(var.set_interval_ms(config[CONF_INTERVAL].total_milliseconds))
