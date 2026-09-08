# Fake printer for testing without hardware

`fake_moonraker.py` is a stand-alone Moonraker (Klipper) printer written in
plain Python. JusPrin talks to it through the same network code it uses for a
real Klipper printer, so tests exercise the app's real upload, print, and
status paths. It answers the endpoints JusPrin actually calls and adds a
`/sim` control API so a person or an AI agent can change the printer's state
and read back what the app sent.

Moonraker was chosen over Bambu because Bambu printers speak to a closed
network plugin over MQTT with TLS plus FTPS, while Moonraker is plain HTTP and
a websocket. Only the standard library is used, and the tests run in about
ten seconds:

```bash
python3 -m unittest discover -s tests/printer_sim -v
```

## What it simulates

| JusPrin code | What the simulator answers |
|---|---|
| Print button (`src/slic3r/Utils/Moonraker.cpp`) | `GET /server/info`, `GET /server/files/roots`, `POST /server/files/upload`, `POST /printer/print/start` |
| Moonraker printer agent (`src/slic3r/Utils/MoonrakerPrinterAgent.cpp`) | `GET /server/info` on connect, `GET /printer/objects/query`, `GET /printer/objects/list`, `ws://…/websocket` (`server.connection.identify`, `printer.objects.subscribe`, `notify_status_update` once a second), `POST /printer/gcode/script`, `GET /server/database/item?namespace=lane_data` (AFC filament lanes) |

A started print heats up and progresses to 100 % on its own over
`--print-seconds` (default 60). The G-code interpreter understands `G28`,
`M104`/`M109`, `M140`/`M190`, `M106`/`M107`, `SET_HEATER_TEMPERATURE`,
`TURN_OFF_HEATERS`, `PAUSE`, `RESUME`, `CANCEL_PRINT`, `SDCARD_PRINT_FILE`,
`M112` and `FIRMWARE_RESTART`; everything else is accepted and logged.

## Control API

Everything under `/sim` is the simulator's own, not Moonraker's.

| Request | Effect |
|---|---|
| `GET /sim` | Printer state (`state`, `klippy_state`, `filename`, `progress`, `extruder`, `heater_bed`, `fan`, `offline`), uploaded `files`, and the last 50 log entries |
| `POST /sim` with JSON | Change state; returns the new state. Unknown keys are rejected so typos do not pass silently |
| `GET /sim/log` | Every request the app made, oldest first: `time`, `method`, `path`, one-line `summary` |
| `DELETE /sim/log` | Clear the log between steps |
| `GET /server/files/gcodes/<name>` | Download a file the app uploaded |

Keys accepted by `POST /sim`: `state` (`standby`, `printing`, `paused`,
`complete`, `cancelled`, `error`), `klippy_state` (`ready`, `startup`,
`shutdown`, `error`, `disconnected`), `filename`, `progress` (0 to 1),
`extruder_temp`, `extruder_target`, `bed_temp`, `bed_target`, `fan` (0 to 1),
`homed`, `message`, `offline` (true makes the printer stop answering, the
websocket included), `auto` (false freezes heating and progress), `lanes`
(replaces the AFC filament lanes), `clear_log`, `reset`.

```bash
curl -s -X POST http://127.0.0.1:7125/sim -d '{"state":"printing","filename":"benchy.gcode","progress":0.42}'
curl -s -X POST http://127.0.0.1:7125/sim -d '{"state":"paused"}'
curl -s -X POST http://127.0.0.1:7125/sim -d '{"klippy_state":"shutdown"}'
curl -s -X POST http://127.0.0.1:7125/sim -d '{"offline":true}'
curl -s -X POST http://127.0.0.1:7125/sim -d '{"reset":true}'
```

## What JusPrin shows

Open the printer half of the header chip. The first row of its menu is the
printer name with the connection state, computed by
`SetupCommands::printer_connection` from the status the agent receives:

| Simulator | Menu row |
|---|---|
| running, no print | **Idle** |
| running a print, started from JusPrin or with `POST /sim {"state":"printing"}` | **Printing** |
| stopped, or `POST /sim {"offline":true}` | **Offline**, about 30 seconds after the last status |
| the selected preset has no print host | **Not connected** |

The chip itself reads **Fake Moonraker · 0.4**. A few seconds after launch the
agent connects, reads the initial status, and opens the websocket. If the
simulator goes away the agent retries with growing delays (1 s, 2 s, 4 s, up
to ten attempts); restart the simulator within a few minutes and the row
returns to Idle on its own. After ten failed attempts, reselect the printer.

## Setup, once

JusPrin's shell hides the dialog that would let you type a print host, so the
simulator installs the printer preset. With JusPrin **closed**:

```bash
python3 tests/printer_sim/fake_moonraker.py --install-preset
```

This writes `user/default/machine/Fake Moonraker 0.4 nozzle.json` into the
JusPrin data directory (`~/Library/Application Support/JusPrin2` on macOS,
`~/.config/JusPrin2` on Linux, `%APPDATA%\JusPrin2` on Windows) and selects it
as the current printer. The preset inherits the `MyKlipper 0.4 nozzle` system
preset from the Custom vendor (`--inherits` picks another installed Klipper
preset, `--datadir` another data directory, `--port` another port). JusPrin
rewrites its config on exit and the chip's menu has no preset picker, so an
installer run while the app is open is lost.

One current limitation: the agent dials `https://` while the app setting
`enable_ssl_for_mqtt` is true, which is the default, and then fails with an
SSL connect error in the app log. Until that is fixed in the app, set it to
false in `JusPrin2.conf` (with the app closed):

```bash
python3 -c "import json,os; p=os.path.expanduser('~/Library/Application Support/JusPrin2/JusPrin2.conf'); c=json.load(open(p)); c.setdefault('app',{})['enable_ssl_for_mqtt']='false'; json.dump(c,open(p,'w'),indent='\t',ensure_ascii=False)"
```

The installer does not change this setting itself because it also governs
Bambu printers' LAN connections.

## Manual test: printer status

1. Start the simulator and leave the terminal open:
   `python3 tests/printer_sim/fake_moonraker.py`
2. Start JusPrin. Within a few seconds the simulator prints `server info`,
   `status query`, `websocket connected`, and `subscribe: …`.
3. Click the printer half of the chip. The first row reads **Fake Moonraker ·
   Idle**. Click elsewhere to close the menu.
4. In another terminal: `curl -s -X POST http://127.0.0.1:7125/sim -d '{"state":"printing","filename":"benchy.gcode","progress":0.4}'`.
   Reopen the menu: **Printing**.
5. `curl -s -X POST http://127.0.0.1:7125/sim -d '{"state":"complete"}'`.
   Reopen the menu: **Idle**.
6. Press Ctrl+C in the simulator's terminal. Wait 30 seconds, reopen the menu:
   **Offline**.
7. Start the simulator again. Within about a minute the menu is back to
   **Idle**, and the simulator has printed a new `websocket connected` line.

## Manual test: sending a print

1. With the simulator running, import a model file into JusPrin (drag an STL
   in, or use the Import button). Do not open a saved 3mf project: a project
   carries its own printer preset and switches away from the fake printer.
2. Press **Slice**, then **Print**, then **Upload and Print** in the dialog.
3. The simulator prints four lines: storage roots, server info, the upload
   with its byte count, and the print start. Reopen the chip menu:
   **Printing**. `curl -s http://127.0.0.1:7125/sim` shows the filename and
   the progress climbing to 1.0 over a minute, after which the menu says
   **Idle** again.

## Automated test (AI agent or script)

1. Start the simulator on a free port with a short print, quiet:
   `python3 tests/printer_sim/fake_moonraker.py --port 7135 --print-seconds 5 --quiet &`
2. Copy the JusPrin data directory to a scratch folder, clear
   `recent_projects` in its `JusPrin2.conf`, set `enable_ssl_for_mqtt` to
   `"false"` (see above), and install the preset into it:
   `python3 tests/printer_sim/fake_moonraker.py --install-preset --datadir <dir> --port 7135`
3. Launch the built app against it with a model, and give the agent runtime a
   dummy key so it does not open a Keychain prompt:
   `OPENAI_API_KEY=x <app>/Contents/MacOS/OrcaSlicer --datadir <dir> tests/data/20mm_cube.obj &`
4. Connection check: within about 10 seconds `GET /sim/log` contains
   `/server/info`, `/printer/objects/query`, `websocket connected`, and a
   `subscribe:` entry. The app's own log under `<dir>/log/` contains
   `MoonrakerPrinterAgent: connect_printer completed`.
5. Print check: slice and send (see below), then assert that `GET /sim/log`
   ends with `/server/files/upload` and `/printer/print/start`, and that
   `GET /sim` reports `"state": "printing"` with the uploaded filename. Fetch
   the file back with `GET /server/files/gcodes/<name>` to inspect the
   G-code the app produced.
6. State checks: `POST /sim` with `offline`, `state`, temperatures, or
   progress, and read the app's reaction from its log or from screenshots of
   the chip menu (the desktop-control tools can open it with a click on the
   printer half of the chip once the app is frontmost).

On macOS the app can be driven without a screen by posting key events to its
process. This also works while the Mac is locked, where screenshot tools are
refused; mouse events are not delivered that way:

```bash
swiftc -O -o /tmp/mac_post_key tests/printer_sim/mac_post_key.swift
PID=$(pgrep -f "OrcaSlicer --datadir <dir>")
/tmp/mac_post_key $PID 15 cmd            # Cmd+R: slice the plate
sleep 12
/tmp/mac_post_key $PID 5 cmd shift       # Cmd+Shift+G: open the send dialog
sleep 4
/tmp/mac_post_key $PID 48                # Tab: focus the Upload button
/tmp/mac_post_key $PID 124               # Right: move to Upload and Print
/tmp/mac_post_key $PID 49                # Space: press it
curl -s http://127.0.0.1:7135/sim/log    # expect server info, upload, print start
```

This is the sequence that verified the whole path on 2026-09-07 and
2026-09-08: the app checked `/server/info`, uploaded the sliced cube (about
340 KB), started the print, and the chip menu showed Idle, Printing, and
Offline as the simulator's state changed.

## Known limits

- Opening a saved project switches to the printer stored in it. Import a
  model file instead, or pick the fake preset again under "Printer settings…".
- The websocket speaks only the methods the agent uses and pushes the full
  subscribed objects every second rather than diffs.
- No authentication and no Happy Hare `mmu` object.
- `mac_post_key.swift` is macOS only; on Linux and Windows drive the app with
  a desktop-automation tool.
