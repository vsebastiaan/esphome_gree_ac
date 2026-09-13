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

CONF_TURBO_SELECT = "turbo_select"
CONF_PLASMA_SELECT = "plasma_select"
CONF_BEEPER_SELECT = "beeper_select"
CONF_SLEEP_SELECT = "sleep_select"
CONF_XFAN_SELECT = "xfan_select"
CONF_SAVE_SELECT = "save_select"

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

ON_OFF_OPTIONS = [
    "Uit",
    "Aan",
]

DISPLAY_OPTIONS = ON_OFF_OPTIONS

tosot_ac_ns = cg.esphome_ns.namespace("tosot_ac")
TosotGWH18AC = tosot_ac_ns.class_(
    "TosotGWH18AC", cg.Component, uart.UARTDevice, climate.Climate
)
TosotACSwitch = tosot_ac_ns.class_("TosotACSwitch", switch.Switch, cg.Component)
TosotACSelect = tosot_ac_ns.class_("TosotACSelect", select.Select, cg.Component)

select_schema = select.select_schema(select.Select).extend(
    {cv.GenerateID(CONF_ID): cv.declare_id(TosotACSelect)}
)

SCHEMA = climate.climate_schema(climate.Climate).extend(uart.UART_DEVICE_SCHEMA)
CONFIG_SCHEMA = cv.All(
    SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(TosotGWH18AC),
            cv.Optional(CONF_KICK_PIN): pins.gpio_input_pin_schema,

            # Use selects for every GWH18 sub-control. Homey exposes these on the
            # climate device, while standalone ESPHome switch entities were not
            # shown in the same Homey control screen.
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
                CONF_TURBO_SELECT, default={"name": "EXP - Turbo"}
            ): select_schema,
            cv.Optional(
                CONF_SLEEP_SELECT, default={"name": "EXP - Sleep"}
            ): select_schema,
            cv.Optional(
                CONF_XFAN_SELECT, default={"name": "EXP - X-Fan"}
            ): select_schema,
            cv.Optional(
                CONF_SAVE_SELECT, default={"name": "EXP - Save / Eco"}
            ): select_schema,
            cv.Optional(
                CONF_PLASMA_SELECT, default={"name": "EXP - Health / Plasma"}
            ): select_schema,
            cv.Optional(
                CONF_BEEPER_SELECT, default={"name": "EXP - Beeper"}
            ): select_schema,
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
        CONF_TURBO_SELECT: ON_OFF_OPTIONS,
        CONF_SLEEP_SELECT: ON_OFF_OPTIONS,
        CONF_XFAN_SELECT: ON_OFF_OPTIONS,
        CONF_SAVE_SELECT: ON_OFF_OPTIONS,
        CONF_PLASMA_SELECT: ON_OFF_OPTIONS,
        CONF_BEEPER_SELECT: ON_OFF_OPTIONS,
    }
    for key, options in select_options.items():
        conf = config[key]
        entity = await select.new_select(conf, options=options)
        await cg.register_component(entity, conf)
        cg.add(getattr(var, f"set_{key}")(entity))
