# Print hosts without a printer

Docker containers that run the real print-host software, for testing the app's
Moonraker and OctoPrint paths: connecting, API keys, status, cameras, and
sending a print. Only the printer boards and the cameras' pictures are
simulated. Bambu printers are covered by the in-process fake instead
([Fake Bambu README](../../src/slic3r/GUI/JusPrin/Testing/README.md)).

| Address | What answers | Key |
|---|---|---|
| `http://127.0.0.1:7130` | Moonraker on Klipper, trusts every address | none |
| `http://127.0.0.1:7131` | the same Klipper through a second Moonraker | needed; HTTP 401 without it |
| `http://127.0.0.1:5030` | OctoPrint 1.11.2 with its bundled virtual printer | needed; HTTP 403 without it |
| `http://127.0.0.1:7140` | the printers' cameras ([Cameras](#cameras)) | none |

Klipper's board is simulavr, an AVR simulator running Klipper's own firmware,
set up from [prind](https://github.com/mkuf/prind). OctoPrint's printer is its
[Virtual Printer](https://docs.octoprint.org/en/devel/bundledplugins/virtual_printer.html)
plugin, which acts like a Marlin printer on a serial port.

## Start and stop

Needs Docker (Docker Desktop on macOS and Windows).

```bash
docker compose -f tests/printer_hosts/docker-compose.yml up -d
```

The first start builds the simulavr image, which nobody publishes
([simulavr/Dockerfile](simulavr/Dockerfile)): a few minutes, most of it
downloading prind's Klipper tools image (1.6 GB to download, about 5.5 GB on
disk with the built image). Klipper reports `ready` some
seconds after the containers start:

```bash
curl -s http://127.0.0.1:7130/printer/info
```

Print the keys as shell exports (`JP_MOONRAKER_KEY`, `JP_OCTOPRINT_KEY`, and a
wrong one, `JP_BAD_KEY`):

```bash
eval "$(tests/printer_hosts/keys.sh)"
```

OctoPrint's key is fixed in `docker-compose.yml`. Moonraker makes its own key
on first start and keeps it until the volumes are removed. OctoPrint's web page
signs in as `admin` / `jusprin-test`.

```bash
docker compose -f tests/printer_hosts/docker-compose.yml down       # keeps keys and uploaded files
docker compose -f tests/printer_hosts/docker-compose.yml down -v    # back to a first start
```

The simulated board uses most of a CPU core for as long as it runs, so stop the
stack when you are done.

## Connecting a printer in the app

Use a printer that is not made by Bambu Lab, such as the generic Klipper
printer, and give the app an address from the table above, with
Moonraker or OctoPrint as the host type. When the app asks for an API key, type
the one `keys.sh` prints, a wrong one, or none, depending on the case. Each
address answers the way a real host does:

| Address and key | What the app's connection test gets |
|---|---|
| `7130`, no key | accepted; Klipper is ready |
| `7131` or `5030`, no key or a wrong key | refused (HTTP 401 or 403), so the app asks for a key |
| `7131` or `5030`, the right key | accepted |

## States worth testing

- **Ready, printing, paused, cancelled, complete:** send G-code with the app,
  or through the API (`/server/files/upload` with `print=true`,
  `/printer/print/pause`, `/printer/print/cancel`). The simulated Klipper
  board runs at half speed (see [simulavr/Dockerfile](simulavr/Dockerfile)),
  so its prints take twice their estimate: the app's 20 mm cube on the
  generic Klipper printer, estimated at 10 minutes, takes about 20.
  OctoPrint's virtual printer keeps its own pace.
- **Klipper shut down behind a working Moonraker:** `docker compose … stop simulavr`.
  Moonraker then reports Klipper's state as `shutdown`, "Lost communication
  with MCU 'mcu'", as a real printer whose board has dropped off does. Start
  `simulavr` again, then restart `klipper` (a `FIRMWARE_RESTART` cannot reset
  the simulated board).
- **Host offline:** stop the Moonraker or OctoPrint container.
- **Nothing listening:** any other port, such as `7139`.
- **Wrong host type:** OctoPrint at `7130` (Moonraker answers OctoPrint's API
  through its `[octoprint_compat]` section, so this connects), or Moonraker at
  `5030` (HTTP 404).

## Cameras

Each printer has a camera, served by [go2rtc](https://github.com/AlexxIT/go2rtc)
in the `camera` container. A camera films a test pattern labelled with its
printer and a clock (UTC), so a frozen feed is easy to spot. Open these in a
browser on the same computer:

| Printer | Live feed (MJPEG) | One picture (JPEG) |
|---|---|---|
| Klipper, both Moonrakers | `http://127.0.0.1:7140/api/stream.mjpeg?src=klipper` | `http://127.0.0.1:7140/api/frame.jpeg?src=klipper` |
| OctoPrint | `http://127.0.0.1:7140/api/stream.mjpeg?src=octoprint` | `http://127.0.0.1:7140/api/frame.jpeg?src=octoprint` |

The hosts hand out the same addresses, as real ones do:

- Moonraker lists the camera at `/server/webcams/list`, from the `[webcam printer]`
  section of [open.conf](moonraker/open.conf) and [key.conf](moonraker/key.conf).
- OctoPrint shows it on its Control tab, and lists it in `/api/settings` under
  `plugins.classicwebcam`. Every start sets it from `WEBCAM_STREAM` and
  `WEBCAM_SNAPSHOT` in `docker-compose.yml`.

These addresses work only on this computer. OctoPrint's server also fetches the
picture address itself, for timelapses, and inside its container `127.0.0.1`
is not this computer, so that fetch fails.

The feeds run at 10 frames a second. A camera's ffmpeg runs only while
something is watching it, so cameras nobody watches cost nothing.

To test a camera that is offline: `docker compose … stop camera`. The hosts
keep listing the camera, but its address no longer answers.

If your stack was started before the cameras were added, run `down` and then
`up -d` (without `-v`) once, so the hosts reload their settings.

## The printer

`klipper/printer.cfg` matches the app's generic Klipper printer (MyKlipper 0.4
nozzle: 250 × 250 × 250, 0.4 mm nozzle, 1.75 mm filament) and defines the
`PRINT_START` and `PRINT_END` macros its start and end G-code call, so that
printer's G-code prints unchanged. simulavr's endstops always read as pressed
and its thermistors read about 103 °C, so homing sets the position without
moving, and M109/M190 set a temperature without waiting for it. Status reports
the target temperatures, but the measured ones stay near 103 °C.

## With the shell harness

`--print-host-check` takes a file of cases, one per line, and runs the app's
connection tool on each without a model:

```
moonraker http://127.0.0.1:7130
moonraker http://127.0.0.1:7131 JP_MOONRAKER_KEY
octoprint http://127.0.0.1:5030 JP_BAD_KEY
```

Export the keys first, as above.

## Versions

Every image is pinned. `KLIPPER_TAG` in [.env](.env) picks the
`mkuf/klipper` tag for both Klipper and the simulated board, whose firmware is
built from that tag's `-tools` image, so the two always match. After changing
it, rebuild with `docker compose … build simulavr`. The cameras are
`alexxit/go2rtc:1.9.14`.
