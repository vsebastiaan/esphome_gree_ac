# Tosot GWH18AAD-K6DNA1B/I — ESPHome UART control on Wemos D1 mini

This document describes the hardware and UART behaviour measured on a real Tosot GWH18AAD-K6DNA1B/I while replacing the original CS532AE-style Wi-Fi module with a Wemos D1 mini / ESP8266.

The important result after the full reverse-engineering session is that the protocol itself was not the startup problem. The AC answered normal polls, but the original resistor-divider RX input on the ESP8266 was unreliable at cold start. A second NPN transistor stage on the AC->D1 direction fixed RX startup immediately.

> Use at your own risk. The AC-side UART is not a native 3.3 V ESP8266 interface. Use the transistor interfaces below rather than connecting the AC UART directly to the D1.

## Tested serial settings

- 4800 baud
- 8 data bits
- even parity
- 1 stop bit (`8E1`)
- D1 `GPIO1` = TX
- D1 `GPIO3` = RX
- both UART directions are inverted in software because both hardware interface stages use an NPN transistor

## Confirmed AC connector wiring

- **ORANGE = AC TX** -> D1 RX interface
- **BLACK = AC RX** <- D1 TX interface
- **BROWN = GND**

## Final hardware interface

### AC TX -> D1 RX

```text
ORANGE (AC TX)
     |
    22k
     |
     B
   2N3904
     C --------+-------- GPIO3 / RX
               |
              10k
               |
              3.3V
     E
     |
BROWN / GND
```

This stage inverts the signal, therefore `GPIO3` is configured with `inverted: true`.

### D1 TX -> AC RX

```text
GPIO1 / TX
     |
    4.7k
     |
     B
   2N3904
     C ---------------- BLACK (AC RX)
     E
     |
BROWN / GND
```

This stage also inverts the signal, therefore `GPIO1` is configured with `inverted: true`.

## Why the second transistor matters

The original prototype used a 4.7k/10k divider from ORANGE to GPIO3. The UART protocol was already correct: the AC answered normal `2F/01` polls. The failure was that the ESP8266 sometimes saw `rx_bytes=0` after a cold start even though the AC was transmitting.

During debugging, an external sniffer made GPIO3 begin receiving. Once reception had started, the sniffer could be removed and RX remained stable. Static resistor changes and multiple GPIO14/D5 kick experiments were then tried, including different series resistances and direct connection. Those experiments could not make startup deterministic.

Replacing the divider with the NPN RX stage above solved the startup problem immediately on the tested unit. The likely benefit is that GPIO3 now sees a clean local 0/3.3 V digital signal referenced to the ESP8266 supply instead of a marginal analog divider waveform from the AC side.

The earlier D5 startup-kick is therefore **not part of the final recommended hardware**. The driver may retain optional kick support for historical/diagnostic use, but the final tested wiring does not require it.

## Known-good ESPHome UART configuration

```yaml
uart:
  id: ac_uart
  tx_pin:
    number: GPIO1
    inverted: true
  rx_pin:
    number: GPIO3
    inverted: true
  baud_rate: 4800
  data_bits: 8
  parity: EVEN
  stop_bits: 1
  rx_buffer_size: 512
```

No `kick_pin` is required with the final dual-transistor interface.

See `examples/tosot-gwh18aad-live-test.yaml` for a complete test configuration.

## Hardware-verified GWH18 functions

### Climate modes

- Off
- Auto
- Cool
- Dry
- Fan only
- Heat

### Fan speed

- Auto
- Low
- Medium
- High

### Target temperature

The normal protocol range is 16–30 °C.

### Vertical louvre

The generic Gree/Sinclair swing table is not correct as a user-facing mapping for this GWH18. Physical testing produced this useful subset:

| UART value | Tested GWH18 behaviour |
|---:|---|
| 1 | full vertical swing |
| 2 | highest fixed position |
| 3 | high fixed position |
| 4 | middle fixed position |
| 5 | low fixed position |
| 6 | lowest fixed position |
| 7–11 | behaved like full swing on the tested unit |

The GWH18-specific component therefore exposes only:

- Swing
- Highest
- High
- Middle
- Low
- Lowest

The generic ESPHome `swing_mode` control is intentionally not advertised for this model.

### Indoor display

Only two useful states are exposed:

- Off
- On

When on, this unit shows the set temperature.

## Automatic UI entities

The GWH18 component now creates the normal controls and experimental test switches by default, so a minimal climate block is enough. The automatically exposed entities are:

- `Fan snelheid`
- `Verticale lamel`
- `Display`
- `EXP - Turbo`
- `EXP - Sleep`
- `EXP - X-Fan`
- `EXP - Save / Eco`
- `EXP - Health / Plasma`
- `EXP - Beeper`

## Experimental functions

### EXP - Turbo

This is the highest-priority remaining verification. Related Gree UART captures indicate report byte 10 changes between normal and Turbo states:

- Cool normal: `0x06`
- Cool Turbo: `0x07`
- Heat normal: `0x0E`
- Heat Turbo: `0x0F`

### Other experimental switches

- `EXP - Sleep`
- `EXP - X-Fan`
- `EXP - Save / Eco`
- `EXP - Health / Plasma`
- `EXP - Beeper`

These are based on closely related Gree/Sinclair packet mappings and should be promoted only after physical GWH18 verification.

## 8 °C frost-protection / steady heat

Tosot/Gree products can expose a separate 8 °C heating/absence mode. It is not simply a normal 8 °C target because the normal target-temperature field starts at 16 °C.

The higher-level Gree Wi-Fi property is commonly called `StHt`, but the exact write bit in this local `2F/31` UART dialect has not yet been proven. The v5 `RX DELTA` logging is intended to identify it with the original remote before adding an `EXP - 8°C verwarming` control.

## Unknown / extra telemetry

The driver logs valid non-`0x31` responses separately as `RX OTHER`. This leaves room to investigate whether the unit exposes additional values such as compressor frequency, coil temperature, outdoor-unit values or other service information. Such fields should not be published as named sensors until a repeatable correlation is demonstrated.

## Project status

The replacement module is now functionally proven on the test unit:

- deterministic RX startup using the dual-transistor interface
- continuous valid `2F/31` reports
- bidirectional control
- operating modes mapped
- fan speed mapped
- target temperature mapped
- vertical-louvre behaviour mapped on real hardware
- display control mapped
- normal controls and experimental test entities exposed automatically

The remaining work is the short experimental-function verification round, especially Turbo, followed by cleanup/merge for the public baseline.
