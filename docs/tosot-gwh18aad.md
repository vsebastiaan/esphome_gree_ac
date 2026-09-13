# Tosot GWH18AAD-K6DNA1B/I — ESPHome UART control on Wemos D1 mini

This document describes the hardware and UART behaviour that was **measured on a real Tosot GWH18AAD-K6DNA1B/I** while replacing the original CS532AE-style Wi-Fi module with a Wemos D1 mini / ESP8266.

The goal is to leave a reproducible path for the next person: what wire is TX/RX, which UART settings work, the unusual RX-startup behaviour seen on the tested unit, and which climate functions have actually been verified instead of merely inherited from a related Gree/Sinclair implementation.

> Use at your own risk. The AC-side UART is not a native 3.3 V ESP8266 interface. Do not connect the AC TX line directly to GPIO3 and do not connect the D5 test/kick pin directly to the RX node.

## Tested serial settings

- 4800 baud
- 8 data bits
- even parity
- 1 stop bit (`8E1`)
- ESP8266 hardware UART
- D1 `GPIO1` = TX
- D1 `GPIO3` = RX
- TX is configured as **inverted in software** because the tested interface uses an NPN transistor on the D1 -> AC direction

## Confirmed wiring

The tested AC connector uses:

- **ORANGE = AC TX** -> must go to **D1 RX / GPIO3**
- **BLACK = AC RX** <- driven by **D1 TX / GPIO1** through the transistor interface
- **BROWN = GND**

### AC TX -> D1 RX divider and D5 test connection

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

## The unusual RX startup problem

This was the hardest part of the prototype and is still the only part that is not yet fully explained.

The AC **does answer the normal `2F/01` poll**. There is no additional protocol handshake required before the AC starts replying. The problem is on the ESP8266 receive side: after some cold starts the D1 sees **zero RX bytes** even though the AC is responding.

The key hardware observation was made with an additional UART sniffer:

1. Without the extra sniffer attached, the D1 could fail to see the AC replies at startup.
2. As soon as the sniffer was attached to the UART lines, the D1 started receiving valid `2F/31` frames.
3. After reception had started, removing the extra sniffer connection did **not** stop communication; the D1 kept receiving normally.
4. This rules strongly against a missing protocol initialization sequence and points instead to an electrical/input-state effect on the D1 RX node during startup.

Several static bias/divider changes were tried during debugging without producing a reliable final solution, including alternate pull/bias resistors and divider values.

A GPIO14/D5 pulse through 4.7 kOhm to the divider midpoint was then added to imitate the electrical disturbance that made the sniffer-assisted startup work:

1. D5 is normally input/high-impedance.
2. During a kick it is driven HIGH briefly through the 4.7 kOhm series resistor.
3. UART polling is paused during the pulse and any garbage is discarded.
4. D5 returns to input/high-Z and normal `2F/01` polling resumes.

This kick **often starts reception immediately and initially appeared fully reliable**, but later testing showed that some boots still require many retries before the D1 begins seeing RX bytes. Once RX starts, communication is stable and valid `2F/31` reports continue normally.

Therefore the current evidence is:

- normal Gree/Tosot polling is sufficient at protocol level;
- the AC is replying from the beginning;
- the remaining startup issue is electrical/receiver-side;
- the D5 kick is a promising workaround, but not yet proven 100% deterministic.

The most useful remaining hardware experiment is to reproduce the sniffer's electrical loading/reference effect rather than invent extra UART handshake packets.

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

The current diagnostic climate component also uses:

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
- On (shows set temperature)

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

## Unknown / extra telemetry

The driver logs valid non-`0x31` responses separately as `RX OTHER`. Gree UART reverse engineering has observed `0x33` frames whose meaning is still unknown.

This leaves room to investigate whether the unit exposes additional values such as compressor state/frequency, coil temperature, outdoor-unit values or other service information. None of those values should be named or published as sensors until a repeatable correlation has been demonstrated.

## Project status

The basic UART/control path is proven on the test unit:

- normal `2F/01` polling receives valid `2F/31` state reports once RX is active;
- bidirectional control works;
- mode, fan, target temperature, display and vertical-louvre behaviour are mapped on real hardware;
- the remaining engineering issue is making RX startup deterministic without relying on the external sniffer.

The D5 kick remains in the diagnostic build because it often bootstraps RX successfully, but the project should not describe it as a final guaranteed startup solution until repeated cold-start testing proves that.
