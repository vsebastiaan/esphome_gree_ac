import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import climate, uart
from esphome.const import CONF_ID

DEPENDENCIES = ["uart"]

CONF_KICK_PIN = "kick_pin"

tosot_ac_ns = cg.esphome_ns.namespace("tosot_ac")
TosotAC = tosot_ac_ns.class_("TosotAC", cg.Component, uart.UARTDevice, climate.Climate)

SCHEMA = climate.climate_schema(climate.Climate).extend(uart.UART_DEVICE_SCHEMA)
CONFIG_SCHEMA = cv.All(
    SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(TosotAC),
            # High-impedance in normal operation. The C++ driver switches this
            # pin to OUTPUT/HIGH only for a short RX-start kick.
            cv.Optional(CONF_KICK_PIN): pins.gpio_input_pin_schema,
        }
    ),
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await climate.register_climate(var, config)
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    if CONF_KICK_PIN in config:
        pin = await cg.gpio_pin_expression(config[CONF_KICK_PIN])
        cg.add(var.set_kick_pin(pin))
