# Fake Bambu printer (in-process)

Substitutes `FakeBambuAgent` for Bambu's closed network plugin so header menu,
Select Machine, and print can run without hardware. It is not a network fake:
MQTT, FTPS, and discovery are not implemented.

Moonraker and OctoPrint printers run against the real host software in Docker
([print hosts README](../../../../../tests/printer_hosts/README.md)). Do not
add a Bambu network fake unless asked.

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

Keep a **Bambu Lab printer** selected: a real system preset, typically
`Bambu Lab A1 mini 0.4 nozzle`, or a named printer that inherits one.
`is_bbl_vendor()` must be true; it reads the inherited `printer_model`, so
either works. An embedded 3mf preset named
`Bambu Lab A1 mini 0.4 nozzle(Tape_4_3MF.3mf)` is not enough; the agent stays
`orca`.

**Add a printer…** in the printer menu installs the system preset and saves it
as a named printer (a user profile such as `Bambu Lab A1 mini`), which is what
Home lists. Alternatively copy `Resources/profiles/BBL` into
`<datadir>/system/` and add the A1 mini model to the `models` array in
`JusPrin2.conf`, then select the system preset. Opening
`~/Downloads/Tape_4_3MF.3mf` only helps after that vendor is installed.

Launch a **scratch** `--datadir`. Do not point fake mode at the everyday
config. JusPrin turns `single_instance` on at every start; its instance lock
lives in the data directory, so a scratch `--datadir` runs as its own instance
beside another JusPrin. Do not pass `--no-single-instance`; Boost rejects it.

The fake emulates printer type `N1` (A1 mini). Select Machine lists it as
**JusPrin Fake A1 mini (LAN)**.

For an MCP client, the shell harness does all of this in its own datadir:
`shell_integration_harness --manual-mcp <dir> --fake-printer` saves a
**Fake A1 mini** printer, connects it to the fake and selects it before it
prints `HARNESS MCP FIXTURE READY`. Edits to the conf would not survive: the
fixture rewrites it at startup.

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
| `spools` | loaded AMS spools; see below. Absent leaves whatever was last set |
| `send_fails` | `true` makes the next print or storage upload fail as an upload failure (read at the moment of Send), so OrcaSlicer's send dialog shows its error and reports no success |
| `connect_fails` | a code such as `"5"` (or `true`, which is `"5"`) makes every connect report `ConnectStatusFailed` with that code to the app, as the plug-in does for a printer that refuses; upstream reads `"5"` as a wrong access code and erases the saved one. Nothing connects while it is set. Tests can also call `fail_next_connect(code)` on the agent for one connect |

No `spools` (or an empty array) reports no AMS at all, same as before this
field existed. A non-empty array reports one AMS unit (id `"0"`) with one
tray per spool, up to 4:

```json
{"spools": [{"subBrands": "PLA Matte", "trayType": "PLA", "colour": "5F7D4FFF"}]}
```

`subBrands` is what `PrinterDiscovery.cpp` reads back as the spool's name
when set (ahead of `trayType`), so it is the field worth setting for a
saved/network-printer card or receipt to show something. `trayType` is the
spool's material (Home's Loaded row names it); the fake sends it beside a
`tray_info_idx`, since upstream ignores a type that arrives without one.
`filamentId` sets that id (`"GFA01"` is Bambu PLA Matte); left out, the
id is empty, and Orca's filament sync skips the tray. `colour` is optional: a spool without it reports no colour, and Home
draws no swatch for it.

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

## A real printer, no model

The printer tools are not exposed over MCP, so `probe.call` cannot reach them.
`shell_integration_harness --printer-network-check <dir> <address> <version>`
drives them with a scripted stand-in for the model, on Bambu's real plug-in:
copy `plugins/` from a data folder that has it into `<dir>` first, and pass the
version in its `libbambu_networking_<version>.dylib` name. It expects a P1S in
cloud mode at `<address>`, prints each tool result and note as
`HARNESS NETWORK` lines, and sends the printer nothing. `--simulate-announce` after
the version hands the app that announcement itself 25 seconds in, for when no
printer is on; it tests everything after the plug-in, not the plug-in. Printers announce on UDP
2021. While another program holds that port the plug-in hears nothing, and
`printer_connection_status` reports `port_taken`, so quit whatever
`lsof -nP -iUDP:2021` names first. The plug-in takes the port only as the app
starts, so once that program lets go the status reports `not_listening` until a
restart. Holding the port from a small script is how to test both states.

`--printer-connect-skill <dir> <address> <version>` runs the same chats for
the connection skill; pass `none` as the version to run without the plug-in.
`--open-issue` after the version also calls `issue_report_open`, which opens
the default browser on a filled-in GitHub issue page. Nothing is filed unless
someone presses Submit there, so close the tab. That run keeps its data
folder; the link is in `log/debug_*.log` under "opening an issue page".

## Unit tests

`jusprin_fake_bambu_tests` (Catch2, GUI build). They cover announce JSON,
control-file parsing, and agent id. They do not start the heartbeat unless the
test sets `set_queue_on_main_fn`.

## Agent id

Registered as `jusprin-fake-bambu`. `GUI_App::switch_printer_agent` uses that
id in place of `bbl` when fake mode is on. No other upstream file is involved.
