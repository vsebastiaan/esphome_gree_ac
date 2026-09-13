import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import climate, select, switch, uart
from esphome.const import CONF_ID

AUTO_LOAD = ["select", "switch"]
DEPENDENCIES = ["uart"]

CONF_KICK_PIN = "kick_pin"

CONF_FAN_SPEED_SELECT = "fan_speed_select"
CONF_VERTICAL_SWING_SELECT = "vertical_swing_select"
CONF_DISPLAY_SELECT = "display_select"

CONF_TURBO_SWITCH = "turbo_switch"
CONF_PLASMA_SWITCH = "plasma_switch"
CONF_BEEPER_SWITCH = "beeper_switch"
CONF_SLEEP_SWITCH = "sleep_switch"
CONF_XFAN_SWITCH = "xfan_switch"
CONF_SAVE_SWITCH = "save_switch"

# Exact behaviour measured on the Tosot GWH18 hardware.
FAN_SPEED_OPTIONS = [
    "Auto",
    "Laag",
    "Midden",
    "Hoog",
]

VERTICAL_SWING_OPTIONS = [
    "Swing",
    "Hoogste",
    "Hoog",
    "Midden",
    "Laag",
    "Laagste",
]

DISPLAY_OPTIONS = [
    "Uit",
    "Aan",
]

tosot_ac_ns = cg.esphome_ns.namespace("tosot_ac")
TosotGWH18AC = tosot_ac_ns.class_(
    "TosotGWH18AC", cg.Component, uart.UARTDevice, climate.Climate
)
TosotACSwitch = tosot_ac_ns.class_("TosotACSwitch", switch.Switch, cg.Component)
TosotACSelect = tosot_ac_ns.class_("TosotACSelect", select.Select, cg.Component)

switch_schema = switch.switch_schema(switch.Switch).extend(cv.COMPONENT_SCHEMA).extend(
    {cv.GenerateID(): cv.declare_id(TosotACSwitch)}
)
select_schema = select.select_schema(select.Select).extend(
    {cv.GenerateID(CONF_ID): cv.declare_id(TosotACSelect)}
)

SCHEMA = climate.climate_schema(climate.Climate).extend(uart.UART_DEVICE_SCHEMA)
CONFIG_SCHEMA = cv.All(
    SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(TosotGWH18AC),
            # High-impedance in normal operation. The C++ driver switches this
            # pin to OUTPUT/HIGH only for a short RX-start kick.
            cv.Optional(CONF_KICK_PIN): pins.gpio_input_pin_schema,

            # GWH18 controls are created by default so a minimal climate block
            # cannot accidentally omit part of the Homey/ESPHome UI. Explicit
            # YAML still overrides these names/settings when desired.
            cv.Optional(
                CONF_FAN_SPEED_SELECT, default={"name": "Fan snelheid"}
            ): select_schema,
            cv.Optional(
                CONF_VERTICAL_SWING_SELECT, default={"name": "Verticale lamel"}
            ): select_schema,
            cv.Optional(
                CONF_DISPLAY_SELECT, default={"name": "Display"}
            ): select_schema,
            cv.Optional(
                CONF_TURBO_SWITCH, default={"name": "EXP - Turbo"}
            ): switch_schema,
            cv.Optional(
                CONF_SLEEP_SWITCH, default={"name": "EXP - Sleep"}
            ): switch_schema,
            cv.Optional(
                CONF_XFAN_SWITCH, default={"name": "EXP - X-Fan"}
            ): switch_schema,
            cv.Optional(
                CONF_SAVE_SWITCH, default={"name": "EXP - Save / Eco"}
            ): switch_schema,
            cv.Optional(
                CONF_PLASMA_SWITCH, default={"name": "EXP - Health / Plasma"}
            ): switch_schema,
            cv.Optional(
                CONF_BEEPER_SWITCH, default={"name": "EXP - Beeper"}
            ): switch_schema,
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

    select_options = {
        CONF_FAN_SPEED_SELECT: FAN_SPEED_OPTIONS,
        CONF_VERTICAL_SWING_SELECT: VERTICAL_SWING_OPTIONS,
        CONF_DISPLAY_SELECT: DISPLAY_OPTIONS,
    }
    for key, options in select_options.items():
        conf = config[key]
        entity = await select.new_select(conf, options=options)
        await cg.register_component(entity, conf)
        cg.add(getattr(var, f"set_{key}")(entity))

    for key in [
        CONF_TURBO_SWITCH,
        CONF_PLASMA_SWITCH,
        CONF_BEEPER_SWITCH,
        CONF_SLEEP_SWITCH,
        CONF_XFAN_SWITCH,
        CONF_SAVE_SWITCH,
    ]:
        conf = config[key]
        entity = cg.new_Pvariable(conf[CONF_ID])
        await cg.register_component(entity, conf)
        await switch.register_switch(entity, conf)
        cg.add(getattr(var, f"set_{key}")(entity))
