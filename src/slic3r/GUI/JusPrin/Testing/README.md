# Fake Bambu printer (in-process)

Substitutes `FakeBambuAgent` for the closed Bambu network plugin so JusPrin's
Bambu paths (header chip, Select Machine, print job, AMS JSON) can be driven
without hardware. It is not a network fake: MQTT/FTPS/discovery are not
implemented.

The Klipper/Moonraker network simulator lives on `jusprin-newui-fakeprinter`
under `tests/printer_sim/` and covers the other half of the app.

## Turn it on

1. Quit JusPrin. The app rewrites `JusPrin2.conf` on exit.
2. In `<datadir>/JusPrin2.conf` add:

   ```
   [jusprin]
   fake_printer = true
   ```

3. Select a Bambu preset. Opening `~/Downloads/Tape_4_3MF.3mf` selects
   "Bambu Lab A1 mini 0.4 nozzle" if that project is on disk.
4. Launch with `--datadir` pointing at a scratch copy of the data directory.
   The log should show `switch_printer_agent: printer agent switched to jusprin-fake-bambu`.

The fake emulates printer type `N1` (A1 mini).

## Drive state

Write `<datadir>/jusprin/fake_printer.json` (create the `jusprin` folder if
needed). The fake rereads it about once a second.

```json
{"state":"printing","progress":0.4,"nozzle":210,"bed":60,"offline":false}
```

| Field | Meaning |
|---|---|
| `state` | `idle`, `heating`, `printing`, `pause`, `finished`, `failed` |
| `progress` | 0–1, mapped to `mc_percent` |
| `nozzle`, `bed` | temperatures in °C |
| `offline` | `true` stops `push_status`; after 30 s the chip shows Offline |

Delete the file or set `"offline": false` and `"state":"idle"` to return to Idle.

## Print

With the fake on and a Bambu preset selected, Print opens Select Machine as
usual. Send completes without talking to a printer; the app log records the
file in `FakeBambuAgent: start_local_print file=...`, and the chip shows
Printing unless the control file overrides it.

## Agent id

Registered as `jusprin-fake-bambu`. `GUI_App::switch_printer_agent` uses that
id in place of `bbl` when fake mode is on. No other upstream file is involved.
