# Gree probe diagnostics: probe-diag-v2

The previous report was printed only once, ten seconds after the early startup
probe. A logger that connected later could miss it. Missing output alone did not
establish that an old binary was running.

The component now registers a separate reporting timer in setup(). Every ten
seconds, for as long as the component is running, it logs at INFO level:

```text
[probe-diag-v2] PROBE_RESULT state=DONE sent=7/7 captured_frames=0 (stored, max 8)
```

RUNNING means the seven-step sequence has not yet completed. DONE means it has.
The identifier is part of every periodic report, not inferred from line numbers.
Open logs at INFO or above (DEBUG/VERBOSE also work), including for the gree tag.
Late connections and reconnects do not require a device restart.

If the existing parser retained startup frames, one frame is replayed with each
report as CAPTURED_RX, cycling through the retained frames. At most eight complete
frames of up to GREE_RX_BUFFER_SIZE (52) bytes are retained by the existing code.
These are RAM snapshots and are lost on reboot. A count of zero does NOT establish
that no raw bytes arrived; malformed, incomplete or oversized input may not have
been retained. It does not establish wiring, signal voltage or AC compatibility.

The reporting timer neither restarts the startup probe nor sends extra UART
packets. The startup sequence, polling, decoder, GPIOs and climate settings are
unchanged. log_probe_result() is also callable explicitly from a lambda when an
immediate replay is needed.

## Validation

`python tests/test_probe_diagnostics.py` compiles the actual reporting translation
unit with a fake scheduler/logger, ASan and UBSan. Eight tests cover a logger opened
after two minutes, reconnect, running state, bounded rotating replay, exact byte
replay, invalid lengths/counts and independent instances. These are simulations,
not hardware tests. GitHub Actions additionally validates and compiles the complete
component for d1_mini with ESPHome 2026.7.4, encrypted API, OTA and 4800 8E1 UART.
The compile fixture has dummy credentials and must never be flashed.
