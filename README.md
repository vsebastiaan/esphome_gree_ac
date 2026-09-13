# Project lineage and license

This repository is derived from [`gekkehenkie11/esphome_gree_ac`](https://github.com/gekkehenkie11/esphome_gree_ac), which is itself a fork of [`piotrva/esphome_gree_ac`](https://github.com/piotrva/esphome_gree_ac). The upstream projects are licensed under **GNU GPL-3.0**, and this repository remains published under **GPL-3.0** as well. Original authorship and license terms are intentionally retained and acknowledged.

GitHub currently treats this repository as a standalone repository rather than as a network fork. That does **not** change its project lineage: the upstream work from `gekkehenkie11` and `piotrva` is the foundation this repository started from.

At the same time, this repository has **diverged substantially** from the upstream implementation, especially for the Tosot GWH18AAD-K6DNA1B/I. The GWH18 path uses a hardware-verified dual-transistor UART interface, model-specific `2F/31` protocol handling, a dedicated ESPHome component path, Homey-oriented controls, and additional diagnostics/tests. It should therefore be considered a **substantially modified derivative**, not merely a mirror or a small patch set on top of the upstream fork.

> **Tosot GWH18 / Wemos D1 mini:** this repository contains a hardware-tested UART replacement path for the Tosot GWH18AAD-K6DNA1B/I. It uses 4800 8E1 and a 2N3904 transistor interface in both UART directions, with both ESP8266 UART pins configured inverted. See [`docs/tosot-gwh18aad.md`](docs/tosot-gwh18aad.md) and [`examples/tosot-gwh18aad-live-test.yaml`](examples/tosot-gwh18aad-live-test.yaml).
>
> **Final verified wiring on the tested GWH18:**
> - AC **ORANGE (AC TX)** -> **22k** -> base **2N3904**; emitter -> **BROWN/GND**; collector -> **GPIO3/RX**; **10k pull-up from GPIO3/collector to 3.3V**.
> - D1 **GPIO1/TX** -> **4.7k** -> base **2N3904**; emitter -> **BROWN/GND**; collector -> **BLACK (AC RX)**.
> - **Do not add an external pull-up on BLACK.** The AC side already pulls BLACK/AC-RX high (measured around **4.8V** on the test unit); the transistor therefore only sinks that line low. The external 10k pull-up belongs on the ESP8266 RX collector side, to 3.3V.
>
> The GWH18 profile exposes Dutch Homey-oriented controls: `Ventilatorsnelheid` = Automatisch / Laag / Midden / Hoog / Turbo, `Verticale lamel`, `Display` and `Slaapstand`. Standard climate modes are localized by Homey as Automatisch / Koelen / Verwarmen / Ontvochtigen / Alleen ventileren / Uit. Less certain family-level mappings such as Save/Eco, X-Fan, Health/Plasma and Beeper remain explicit opt-in test controls. No separate GWH18 Quiet/Stil fan mode has been proven or exposed.

# Open source WIFI module replacement for Gree protocol based AC's for Home Assistant.
This repository adds support for ESP-based WiFi modules to interface with Gree/Sinclair AC units.

The original project lineage comes from `piotrva/esphome_gree_ac`, with further work from `gekkehenkie11/esphome_gree_ac`; many thanks to both upstream authors and contributors.

Compared with the original project, this codebase includes among other things:

1) Fixed the fan mode, tested on Gree/Daizuki/TGM AC's.
2) Fixed the dropping of commands.
3) Fixed the rejection of commands.
4) Fixed reporting of current temp.
5) Fixed the Fahrenheit mode.
6) Implemented optional command mute/no-beep behavior for compatible legacy modules. This is beeper suppression, **not** an AC Quiet/Stil fan mode.
7) Added a substantially different, hardware-tested Tosot GWH18AAD-K6DNA1B/I implementation as documented above.
   
It's now compatible with GRJWB04-J / Cs532ae wifi modules, with the additional dedicated GWH18 path documented separately.

# Current state:
No known problems! if you run into an issue though, please let me know.

# HOW TO 
You can flash this to an ESP module. I used an ESP01-M module, like this one:
https://nl.aliexpress.com/item/1005008528226032.html
So that’s both an ESP01 and the ‘adapter board’ for 3.3V ↔ 5V conversion (since the ESP01 uses 3.3V and the AC uses 5V).


Create a new project in the ESPbuilder from Home assistant and use my YAML (from the examples directory), copy or modify the info from the generated YAML to mine, where it says '[insert yours]'.
Then you should be able to compile it. I think you can flash directly from Home Assistant,
but I downloaded the compiled binary and flashed with: https://github.com/esphome/esphome-flasher/releases

See FlashingLayout.jpg for wiring for flashing. Alternatively, if you plan to flash several, it might be worthwile to make an adapted version of the USB-TO-TTL adapter and solder 2 jumpers, which willl put the chip into programming mode immediately, so no more wiring, you can just clip on the chip and flash it right away. See modifiedTTL.jpg

See the 4 module cable photos for wiring (for flashing and the wiring for connecting it to your AC). The connector is a 4 pins “JST XARP-04V”, you can for example order them here: https://es.aliexpress.com/item/1005009830663057.html. Alternatively it's possible to just use dupont cables and then put tape around the 4 ends to simulate the form of a connector (so that it makes it thicker), so that it will sit still in the connector socket, but make sure all 4 cables make connection, I tried that first and you might need to make adjustments because of 1 cable not making solid connection (this might result in errors in the log). So best to use the real connector. 


After you've connected the module to your AC, it should pop under settings/integrations/esphome as a 'new device' and then you can add it to HA. If not, check if it started a WIFI access point, which it will do if it can't connect to your home wifi. You can then connect to that and configure it from there (via 192.168.4.1)

**USE AT YOUR OWN RISK!**
