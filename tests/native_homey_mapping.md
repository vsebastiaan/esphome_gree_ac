# Expected default GWH18 entity model

Compile/documentation guard for the normal profile:

- one ESPHome Climate entity with current temperature
- native fan modes Auto / Low / Medium / High
- native custom fan mode Turbo
- no auto-created `room_temperature_sensor`
- no auto-created `fan_speed_select`
- vertical louver, Display and Sleep remain separate selects

The compile fixture exercises the default profile plus the opt-in experimental controls.
