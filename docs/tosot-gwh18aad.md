# Tosot GWH18AAD-K6DNA1B/I — ESPHome UART control on Wemos D1 mini

This document describes the final hardware and UART behaviour measured on a real Tosot GWH18AAD-K6DNA1B/I while replacing the original CS532AE-style Wi-Fi module with a Wemos D1 mini / ESP8266.

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

**Important pull-up detail:** there is no added external pull-up resistor on BLACK in the final build. The AC itself already pulls BLACK / AC-RX high; this was measured at roughly **4.8 V** on the tested unit. The TX-side 2N3904 is therefore used as an open-collector pull-down: GPIO1 drives its base through **4.7k**, and the transistor pulls BLACK low when active. The added external pull-up in this design is the **10k from GPIO3/RX to 3.3 V** on the opposite, AC-TX -> D1-RX transistor stage.

## Important: disable physical serial logging

The ESP8266 hardware UART TX pin used by the AC is `GPIO1`. ESPHome's normal serial logger also uses that pin unless it is disabled. If serial logging is left enabled, logger output is mixed onto the AC TX line and corrupts the 4800 8E1 protocol.

The failure mode is misleading: the RX path can remain completely healthy with valid `2F/31` reports and zero checksum errors, while outgoing commands are received only intermittently or not at all.

For this GWH18 configuration, always use:

```yaml
logger:
  baud_rate: 0
```

ESPHome logging remains available over the API; this only disables physical UART log output on `GPIO1`.

## Known-good ESPHome UART configuration

```yaml
logger:
  baud_rate: 0

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

See `examples/tosot-gwh18aad-live-test.yaml` for a complete test configuration.

## Hardware-verified GWH18 functions

### Climate modes

ESPHome exposes the normal climate modes; Homey localises these in Dutch as:

- Automatisch
- Koelen
- Verwarmen
- Ontvochtigen
- Alleen ventileren
- Uit

### Ventilatorsnelheid

The user-facing fan selector is deliberately Dutch and contains:

- Automatisch
- Laag
- Midden
- Hoog
- Turbo

Turbo is a separate UART protocol bit internally, but it is presented as the highest fan-speed choice because that is the most natural Homey control. Selecting a normal fan speed clears the Turbo bit again.

### Target temperature

The normal protocol range is 16–30 °C.

### Verticale lamel

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
- Hoogste
- Hoog
- Midden
- Laag
- Laagste

The generic ESPHome `swing_mode` control is intentionally not advertised for this model.

### Display

Only two useful states are exposed:

- Uit
- Aan

When on, this unit shows the set temperature.

### Turbo

Turbo has been physically tested on the GWH18 and works. It is integrated into `Ventilatorsnelheid` instead of being exposed as a separate switch/select.

### Slaapstand

The GWH18 acknowledges the Sleep command with its normal confirmation beep. Sleep is a delayed comfort function, so its complete temperature/fan behaviour is not instantaneously visible during a short bench test. It is exposed by default as `Slaapstand` with `Uit` / `Aan`, while its long-duration behaviour can still be observed in normal use.

## Default UI entities

A minimal climate block automatically creates:

- `Ventilatorsnelheid` — Automatisch / Laag / Midden / Hoog / Turbo
- `Verticale lamel` — Swing / Hoogste / Hoog / Midden / Laag / Laagste
- `Display` — Uit / Aan
- `Slaapstand` — Uit / Aan

The remaining family-level functions are deliberately **not** auto-created. They can still be enabled explicitly for controlled testing.

## Optional / still-to-be-proven functions

### Save / Eco

The tested GWH18 gives its normal acknowledgement beep when the current Save/Eco mapping is sent, but the actual energy-saving behaviour has not yet been demonstrated. A beep alone proves that the indoor unit received a valid-looking command, not that the intended feature is active.

This control is therefore opt-in only. The best follow-up test is on another air conditioner that already has reliable power metering: compare otherwise identical operating periods with Save/Eco off and on, and also verify whether the returned UART state bit follows the requested state.

### X-Fan

On related Gree/Tosot units, X-Fan is an evaporator-drying function rather than an extra normal fan speed. Its effect is expected mainly after switching off from COOL or DRY: the indoor fan may continue for a while to dry the coil. A quick on/off test while the unit is simply running can therefore look like "nothing happens".

The mapping remains opt-in until that shutdown behaviour is reproduced on this GWH18.

### Health / Plasma

The unit acknowledges the mapped command with a beep, but no visible effect has yet been identified. Some related models use this for an ionizer/plasma function that is only meaningful when the corresponding hardware is actually fitted. It therefore remains opt-in.

### Beeper

No useful effect was observed from the current Beeper mapping on the tested GWH18. It remains available only as a protocol-test option and is not created by default.

To enable any of these test controls, add them explicitly to the climate block, for example:

```yaml
climate:
  - platform: tosot_ac
    id: tosot_huiskamer
    name: "Airco huiskamer"
    uart_id: ac_uart

    save_select:
      name: "TEST - Save Eco"
    # xfan_select:
    #   name: "TEST - X-Fan"
    # plasma_select:
    #   name: "TEST - Health Plasma"
    # beeper_select:
    #   name: "TEST - Beeper"
```

## Quiet / stille modus

No separate Quiet/Silent mode has been proven on this GWH18. Related Gree/Sinclair dialects suggest a possible quiet bit, but it is deliberately not exposed as a normal control without hardware proof.

## 8 °C frost-protection / steady heat

Tosot/Gree products can expose a separate 8 °C heating/absence mode. It is not simply a normal 8 °C target because the normal target-temperature field starts at 16 °C.

The higher-level Gree Wi-Fi property is commonly called `StHt`, but the exact write bit in this local `2F/31` UART dialect has not yet been proven. The v5 `RX DELTA` logging is intended to identify it with the original remote before adding an experimental control.

## Unknown / extra telemetry

The driver logs valid non-`0x31` responses separately as `RX OTHER`. This leaves room to investigate whether the unit exposes additional values such as compressor frequency, coil temperature, outdoor-unit values or other service information. Such fields should not be published as named sensors until a repeatable correlation is demonstrated.

## Project status

The normal replacement-module functionality is now reverse-engineered and usable on the tested GWH18:

- deterministic RX startup using the dual-transistor interface
- continuous valid `2F/31` reports
- bidirectional control
- operating modes mapped
- fan speed mapped
- target temperature mapped
- vertical-louvre behaviour mapped on real hardware
- display control mapped
- Turbo physically verified and integrated into fan speed
- Sleep accepted and exposed as Slaapstand
- uncertain family-level functions moved behind explicit opt-in configuration
- no unproven Quiet mode exposed

Any further work is optional protocol exploration rather than required functionality for normal Homey use.
