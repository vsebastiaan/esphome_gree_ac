import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate, select, sensor, uart
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_TEMPERATURE,
    STATE_CLASS_MEASUREMENT,
    UNIT_CELSIUS,
)

# The GWH18 exposes its core HVAC state through ESPHome Climate. Selects are
# retained only for controls that do not fit the standard climate model.
AUTO_LOAD = ["select", "sensor", "switch"]
DEPENDENCIES = ["uart"]

CONF_ROOM_TEMPERATURE_SENSOR = "room_temperature_sensor"
CONF_FAN_SPEED_SELECT = "fan_speed_select"
CONF_VERTICAL_SWING_SELECT = "vertical_swing_select"
CONF_DISPLAY_SELECT = "display_select"

CONF_PLASMA_SELECT = "plasma_select"
CONF_BEEPER_SELECT = "beeper_select"
CONF_SLEEP_SELECT = "sleep_select"
CONF_XFAN_SELECT = "xfan_select"
CONF_SAVE_SELECT = "save_select"

# Legacy/opt-in selector kept for existing explicit YAML configurations. A
# normal GWH18 install now exposes fan speed through the native Climate entity
# (Auto/Low/Medium/High plus custom Turbo) instead.
FAN_SPEED_OPTIONS = [
    "Automatisch",
    "Laag",
    "Midden",
    "Hoog",
    "Turbo",
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
TosotACSelect = tosot_ac_ns.class_("TosotACSelect", select.Select, cg.Component)

select_schema = select.select_schema(select.Select).extend(
    {cv.GenerateID(CONF_ID): cv.declare_id(TosotACSelect)}
)

SCHEMA = climate.climate_schema(climate.Climate).extend(uart.UART_DEVICE_SCHEMA)
CONFIG_SCHEMA = cv.All(
    SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(TosotGWH18AC),

            # Compatibility/debug options only. Room temperature is already a
            # native Climate current_temperature and no extra sensor is needed
            # in a normal installation.
            cv.Optional(CONF_ROOM_TEMPERATURE_SENSOR): sensor.sensor_schema(
                unit_of_measurement=UNIT_CELSIUS,
                accuracy_decimals=1,
                device_class=DEVICE_CLASS_TEMPERATURE,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_FAN_SPEED_SELECT): select_schema,

            # Verified controls that do not have a sufficiently expressive
            # standard Climate representation.
            cv.Optional(
                CONF_VERTICAL_SWING_SELECT, default={"name": "Verticale lamel"}
            ): select_schema,
            cv.Optional(
                CONF_DISPLAY_SELECT, default={"name": "Display"}
            ): select_schema,
            cv.Optional(
                CONF_SLEEP_SELECT, default={"name": "Slaapstand"}
            ): select_schema,

            # Family-level mappings kept for controlled hardware testing only.
            # They are NOT auto-created on a normal GWH18 installation.
            cv.Optional(CONF_XFAN_SELECT): select_schema,
            cv.Optional(CONF_SAVE_SELECT): select_schema,
            cv.Optional(CONF_PLASMA_SELECT): select_schema,
            cv.Optional(CONF_BEEPER_SELECT): select_schema,
        }
    ),
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await climate.register_climate(var, config)
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    if CONF_ROOM_TEMPERATURE_SENSOR in config:
        room_sensor = await sensor.new_sensor(config[CONF_ROOM_TEMPERATURE_SENSOR])
        cg.add(var.set_room_temperature_sensor(room_sensor))

    select_options = {
        CONF_FAN_SPEED_SELECT: FAN_SPEED_OPTIONS,
        CONF_VERTICAL_SWING_SELECT: VERTICAL_SWING_OPTIONS,
        CONF_DISPLAY_SELECT: DISPLAY_OPTIONS,
        CONF_SLEEP_SELECT: ON_OFF_OPTIONS,
        CONF_XFAN_SELECT: ON_OFF_OPTIONS,
        CONF_SAVE_SELECT: ON_OFF_OPTIONS,
        CONF_PLASMA_SELECT: ON_OFF_OPTIONS,
        CONF_BEEPER_SELECT: ON_OFF_OPTIONS,
    }
    for key, options in select_options.items():
        if key not in config:
            continue
        conf = config[key]
        entity = await select.new_select(conf, options=options)
        await cg.register_component(entity, conf)
        cg.add(getattr(var, f"set_{key}")(entity))
