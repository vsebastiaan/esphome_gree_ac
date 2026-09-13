# Tosot GWH18AAD-K6DNA1B/I — ESPHome UART control on Wemos D1 mini

This document describes the hardware and UART behaviour that was **measured on a real Tosot GWH18AAD-K6DNA1B/I** while replacing the original CS532AE-style Wi-Fi module with a Wemos D1 mini / ESP8266.

The goal is to leave a reproducible path for the next person: what wire is TX/RX, which UART settings work, why a seemingly unnecessary GPIO14/D5 connection is required on the tested unit, and which climate functions have actually been verified instead of merely inherited from a related Gree/Sinclair implementation.

> Use at your own risk. The AC-side UART is not a native 3.3 V ESP8266 interface. Do not connect the AC TX line directly to GPIO3 and do not connect the D5 kick pin directly to the RX node.

## Tested serial settings

- 4800 baud
- 8 data bits
- even parity
- 1 stop bit (`8E1`)
- ESP8266 hardware UART
- D1 `GPIO1` = TX
- D1 `GPIO3` = RX
- TX is configured as **inverted in software** because the tested interface uses an NPN transistor on the D1 -> AC direction

These settings match the broader Gree UART reverse-engineering work in `bekmansurov/gree-hvac-protocol`, but the wiring and startup-kick behaviour below were verified on this Tosot unit.

## Confirmed wiring

The tested AC connector uses:

- **ORANGE = AC TX** -> must go to **D1 RX / GPIO3**
- **BLACK = AC RX** <- driven by **D1 TX / GPIO1** through the transistor interface
- **BROWN = GND**

### AC TX -> D1 RX divider and startup kick

```text
AC ORANGE (AC TX)
       |
      4.7k
       |
       +-------------------- GPIO3 / RX
       |
      10k
       |
      GND

GPIO14 / D5
       |
      4.7k
       |
       +-------------------- same RX midpoint
```

**D5/GPIO14 must be connected through the 4.7 kOhm series resistor, never directly.**

### D1 TX -> AC RX

```text
GPIO1 / TX
    |
   4.7k
    |
 base  2N3904
       C ------------------- BLACK (AC RX)
       E
       |
      GND
```

The NPN stage inverts the signal, therefore the ESPHome UART TX pin is configured with `inverted: true`.

## The unusual D5 startup kick

This was the hardest part of the prototype.

On the tested GWH18, normal TX polling alone did not reliably bring the AC UART receive path to life after a cold ESP reset. During debugging, attaching/touching the RX line with the extra sniffer hardware caused communication to start. This pointed to a line-state/startup effect rather than a packet-format problem.

The final working solution uses **GPIO14 / D5 as a temporary RX-line kick**:

1. D5 is normally configured as input/high-impedance.
2. At boot it is driven HIGH for about 100 ms through 4.7 kOhm to the RX divider node.
3. No UART poll is transmitted during the pulse and any garbage bytes are flushed.
4. D5 is returned to input/high-Z.
5. The driver immediately sends the normal poll.
6. Once a valid `2F/31` report arrives, normal communication continues without further kicks.
7. If startup does not succeed, the driver retries. After a working link has been established, the kick is only used as RX-timeout recovery.

Typical successful logging:

```text
[tosot-gwh18-v4] RX KICK start reason=boot count=1
[tosot-gwh18-v4] RX KICK released; pin is high-Z
[tosot-gwh18-v4] SUMMARY ... reports_2F31=... kicks=1 kick_active=no ready=YES
```

On this prototype the D5 kick changed the setup from "works only after the sniffer touches the line" to reliable standalone startup.

## Known-good ESPHome UART configuration

```yaml
uart:
  id: ac_uart
  tx_pin:
    number: GPIO1
    inverted: true
  rx_pin: GPIO3
  baud_rate: 4800
  data_bits: 8
  parity: EVEN
  stop_bits: 1
  rx_buffer_size: 512
```

The climate component also needs:

```yaml
kick_pin: GPIO14
```

See `examples/tosot-gwh18aad-live-test.yaml` for a complete test configuration.

## Hardware-verified GWH18 functions

### Climate modes

The normal operating modes work through the UART driver:

- Off
- Auto
- Cool
- Dry
- Fan only
- Heat

### Fan speed

The normal fan settings work:

- Auto
- Low
- Medium
- High

### Target temperature

The normal protocol range is 16–30 °C.

### Vertical louvre

The generic Gree/Sinclair swing table is **not correct as a user-facing mapping for this GWH18**. Physical testing produced this useful subset:

| UART value | Tested GWH18 behaviour |
|---:|---|
| 1 | full vertical swing |
| 2 | highest fixed position |
| 3 | high fixed position |
| 4 | middle fixed position |
| 5 | low fixed position |
| 6 | lowest fixed position |
| 7–11 | behaved like full swing on the tested unit |

For this reason the GWH18-specific component exposes only values 1–6 instead of pretending the whole family-level 1–11 table has distinct behaviour.

The generic ESPHome `swing_mode` control is intentionally not advertised for this model. Exact vertical-louvre control is clearer and avoids exposing non-existent horizontal behaviour.

### Indoor display

Only the following UI choices were useful on the tested unit:

- Off
- Show set temperature

Other family-level display modes were removed from the GWH18-facing UI after hardware testing.

## Experimental functions

The current test configuration deliberately prefixes unverified functions with `EXP -` so they cannot be confused with the confirmed controls.

### EXP - Turbo

**Important test candidate.** The wider Gree UART reverse engineering strongly supports this mapping. In the `2F/31` report, byte 10 changes between normal and Turbo states:

- Cool normal: `0x06`
- Cool Turbo: `0x07`
- Heat normal: `0x0E`
- Heat Turbo: `0x0F`

The v5 driver maps the low Turbo bit and exposes `EXP - Turbo`. This still needs the final physical GWH18 verification before being promoted to a normal control.

### Other currently exposed experimental switches

- `EXP - Sleep`
- `EXP - X-Fan`
- `EXP - Save / Eco`
- `EXP - Health / Plasma`
- `EXP - Beeper`

These are based on closely related Gree/Sinclair packet mappings. They must be tested on the physical GWH18 before being documented as supported.

## 8 °C frost-protection / steady heat

Tosot/Gree products can expose an **8 °C heating / steady-heat / absence mode**. This is a separate function, not simply a normal 8 °C target temperature; the normal target-temperature field starts at 16 °C.

On Tosot documentation for compatible units, 8 °C heating is entered while in Heat mode by pressing **TEMP + CLOCK simultaneously** on the original remote.

The **Wi-Fi-level** Gree property is commonly called `StHt`, but the corresponding bit in this tested UART `2F/31` dialect has not yet been identified. Therefore the project does not currently transmit a guessed 8 °C bit.

### How to discover it on this unit

1. Flash the v5 diagnostic build with logger level `DEBUG` or `VERBOSE`.
2. Put the AC in normal Heat mode and let the UART state settle.
3. Activate 8 °C heating using the original remote.
4. Watch for lines such as:

```text
RX DELTA byte=N 0xAA->0xBB known_mask=0x.. unknown_bits=0x..
```

5. Disable 8 °C heating again and verify the same bit reverses.
6. Once a repeatable bit is found, add it as a dedicated `EXP - 8°C verwarming` control and test it before promoting it to supported status.

This is safer than guessing an undocumented write bit.

## Unknown / extra telemetry

The driver logs valid non-`0x31` responses separately as `RX OTHER`. Gree UART reverse engineering has observed `0x33` frames whose meaning is still unknown.

This leaves room to investigate whether the unit exposes additional values such as compressor state/frequency, coil temperature, outdoor-unit values or other service information. None of those values should be named or published as sensors until a repeatable correlation has been demonstrated.

## Protocol references

Useful prior art:

- https://github.com/bekmansurov/gree-hvac-protocol — reverse engineered Gree UART protocol and `2C`/`2F,31` packet fields
- https://github.com/cmroche/greeclimate — Gree Wi-Fi property names such as `Tur`, `StHt`, `SvSt`, `Quiet`, `Health` and `Blo`
- https://tosotamerica.com/wp-content/uploads/2020/06/8C-Heating-TOSOT.pdf — Tosot 8 °C heating instructions

The important distinction is that the first source describes the **local UART link**, while `greeclimate` describes the higher-level Wi-Fi protocol. A Wi-Fi property name does not automatically tell us which UART bit to write; hardware verification remains necessary.

## Project status

The basic replacement module is now proven on the test unit:

- standalone boot without the original Wi-Fi module
- reliable RX startup using the D5 kick
- continuous valid `2F/31` reports
- bidirectional control
- mode, fan, target temperature, display and vertical-louvre behaviour mapped on real hardware

The remaining work is a short experimental-function verification round (especially Turbo) and then the tested mapping can be treated as the GWH18 baseline.
