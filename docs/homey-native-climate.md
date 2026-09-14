# GWH18 native climate mapping

The normal Tosot GWH18 profile exposes core HVAC state through the standard ESPHome `Climate` entity.

- Room temperature is `Climate.current_temperature`; no duplicate temperature sensor is required.
- Fan speed uses the native climate fan mode: Auto, Low, Medium and High.
- Turbo is exposed as a native custom climate fan mode named `Turbo`.
- The six-position vertical louver remains a separate `select`, because the standard climate swing enum cannot represent the verified fixed positions accurately.
- Display and Sleep remain separate selects for the same reason.

`room_temperature_sensor` and `fan_speed_select` remain available only as explicit compatibility/debug options. They are no longer auto-created.

This keeps the default device model friendly to generic ESPHome consumers such as Home Assistant and Homey instead of requiring consumer-specific mapping for basic temperature and fan control.
