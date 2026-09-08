# Fake Bambu printer (in-process)

Substitutes `FakeBambuAgent` for Bambu's closed network plugin so header menu,
Select Machine, and print can run without hardware. It is not a network fake:
MQTT, FTPS, and discovery are not implemented.

The Klipper/Moonraker simulator lives on `jusprin-newui-fakeprinter` under
`tests/printer_sim/` and covers print-host printers. Do not add a Bambu network
fake unless asked.

This file is the operator guide for humans and AI agents.

## What success looks like

- Log: `switch_printer_agent: printer agent switched to jusprin-fake-bambu`
- Log: `set_selected_machine=FAKE001` and later `FakeBambuAgent: connected FAKE001`
- Printer chip shows `Bambu Lab A1 mini · 0.4` (nickname and nozzle only)
- Click the **printer** half of that chip. The **first menu row** is Idle,
  Printing, or Offline. Those words are never on the chip itself.

If the log says `moonraker` or `orca`, the fake is not in the path. Stop and
fix the preset; do not patch UI gates.

## Turn it on

Quit JusPrin first. The app rewrites `JusPrin2.conf` on exit.

`JusPrin2.conf` is JSON. Under the scratch datadir:

```json
"jusprin": { "fake_printer": true }
```

JSON `true` is what the app stores; `get("jusprin", "fake_printer")` reads as
the string `"true"`. An INI `[jusprin]` block is ignored.

Keep a **real Bambu Lab system preset** selected, typically
`Bambu Lab A1 mini 0.4 nozzle`. `is_bbl_vendor()` must be true. An embedded
3mf preset named `Bambu Lab A1 mini 0.4 nozzle(Tape_4_3MF.3mf)` is not enough;
the agent stays `orca`.

Install the system preset with **Add a printer…** in the printer menu, or copy
`Resources/profiles/BBL` into `<datadir>/system/` and add the A1 mini model to
the `models` array in `JusPrin2.conf`. Opening `~/Downloads/Tape_4_3MF.3mf`
only helps after that vendor is installed.

Launch a **scratch** `--datadir`. Do not point fake mode at the everyday
config. Set `"single_instance": false` in the `app` object if another JusPrin
is already running. Do not pass `--no-single-instance`; Boost rejects it.

The fake emulates printer type `N1` (A1 mini). Select Machine lists it as
**JusPrin Fake A1 mini (LAN)**.

## Drive state

Write `<datadir>/jusprin/fake_printer.json` (create `jusprin/` if needed). The
fake rereads it about once a second.

```json
{"state":"printing","progress":0.4,"nozzle":210,"bed":60,"offline":false}
```

| Field | Meaning |
|---|---|
| `state` | `idle`, `heating`/`prepare`, `printing`/`running`, `pause`, `finished`, `failed` |
| `progress` | 0–1, mapped to `mc_percent` |
| `nozzle`, `bed` | °C |
| `offline` | `true` stops `push_status`; after ~30 s the menu first row is Offline |

Delete the file, or set `"offline": false` and `"state":"idle"`, to return to Idle.

## Print

Slice, then Print (or Cmd+Shift+G on macOS). Send completes without a printer.
The log records `FakeBambuAgent: start_local_print file=...`.

If the control file is present, that file wins: send still succeeds, but the
menu stays on the file's state. Remove the file if you want the canned printing
scenario after send.

## Isolate a running app (agents)

Another JusPrin may already be open. Do not send keys or clicks to it.

On macOS both processes are often named `OrcaSlicer`. Target by unix pid or by
a unique `CFBundleIdentifier` on a ditto'd `.app` (then ad-hoc `codesign`).
`osascript` `unix id` filters can match the wrong process; prefer
`AXUIElementCreateApplication(pid)` or the unique bundle id.

Keyboard events posted to the pid work while the Mac is locked; mouse events
posted that way do not. Menus only open when that app is frontmost. The printer
half of the chip is a wx control (`SetName("Printer")`) and often has no AX
title; click by coordinates after a screenshot, or use the documented shortcut
for Print.

Logs: `<datadir>/log/debug_*.log`. This machine's default `grep` may be ugrep;
use `/usr/bin/grep -a` on app logs.

## Unit tests

`jusprin_fake_bambu_tests` (Catch2, GUI build). They cover announce JSON,
control-file parsing, and agent id. They do not start the heartbeat unless the
test sets `set_queue_on_main_fn`.

## Agent id

Registered as `jusprin-fake-bambu`. `GUI_App::switch_printer_agent` uses that
id in place of `bbl` when fake mode is on. No other upstream file is involved.
