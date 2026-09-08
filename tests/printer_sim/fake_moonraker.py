#!/usr/bin/env python3
"""Fake Moonraker printer for testing JusPrin without hardware.

Speaks the subset of the Moonraker HTTP API that JusPrin actually calls:
the Print button (Moonraker.cpp: /server/info, /server/files/roots,
/server/files/upload, /printer/print/start) and the "moonraker" printer
agent (MoonrakerPrinterAgent.cpp: /server/info on connect,
/printer/gcode/script, /printer/objects/query, /server/database/item for
AFC lane_data filament sync). On top of that it exposes a /sim control API
so a person or an AI agent can set printer state and read back what the app
sent. Standard library only.

Run the printer:            python3 tests/printer_sim/fake_moonraker.py
Point JusPrin at it once:   python3 tests/printer_sim/fake_moonraker.py --install-preset
Look at it:                 curl -s http://127.0.0.1:7125/sim
Change it:                  curl -s -X POST http://127.0.0.1:7125/sim -d '{"state":"printing","progress":0.4}'
"""

import argparse
import base64
import email.parser
import email.policy
import hashlib
import json
import os
import socket
import struct
import sys
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, unquote, urlsplit

WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

PRINT_STATES = ("standby", "printing", "paused", "complete", "cancelled", "error")
KLIPPY_STATES = ("ready", "startup", "shutdown", "error", "disconnected")
DEFAULT_PORT = 7125
DEFAULT_PRESET_NAME = "Fake Moonraker 0.4 nozzle"
DEFAULT_MODEL_NAME = "Fake Moonraker"
DEFAULT_PARENT_PRESET = "MyKlipper 0.4 nozzle"


def default_lanes():
    """Four AFC lanes in the shape MoonrakerPrinterAgent::fetch_moonraker_filament_data reads."""
    spools = [("PLA", "#F4EE2A", 60, 210), ("PETG", "#26A69A", 70, 240), ("ABS", "#FF0000", 100, 250), ("TPU", "#000000", 40, 225)]
    lanes = {}
    for index, (material, color, bed, nozzle) in enumerate(spools, start=1):
        lanes["lane%d" % index] = {
            "lane": str(index), "material": material, "color": color,
            "bed_temp": bed, "nozzle_temp": nozzle, "spool_id": None, "scan_time": "", "td": "",
        }
    return lanes


class Printer:
    """The simulated printer: state, a tiny G-code interpreter, and a request log."""

    def __init__(self, print_seconds=60.0, gcode_dir=None):
        self.lock = threading.RLock()
        self.print_seconds = float(print_seconds)
        self.gcode_dir = gcode_dir or os.path.join(tempfile.gettempdir(), "fake_moonraker", "gcodes")
        os.makedirs(self.gcode_dir, exist_ok=True)
        self.log = []
        self.database = {"lane_data": default_lanes()}
        self.reset()

    def reset(self):
        with self.lock:
            self.klippy_state = "ready"
            self.offline = False
            self.auto = True
            self.state = "standby"
            self.filename = ""
            self.progress = 0.0
            self.print_duration = 0.0
            self.total_duration = 0.0
            self.message = ""
            self.extruder = {"temperature": 25.0, "target": 0.0}
            self.heater_bed = {"temperature": 25.0, "target": 0.0}
            self.fan_speed = 0.0
            self.homed_axes = ""

    # -- time ---------------------------------------------------------------

    def tick(self, dt):
        """Advance the simulation by dt seconds: heaters approach targets, prints progress."""
        with self.lock:
            if not self.auto:
                return
            for heater, rate in ((self.extruder, 40.0), (self.heater_bed, 10.0)):
                goal = heater["target"] if heater["target"] > 0 else 25.0
                step = rate * dt
                delta = goal - heater["temperature"]
                heater["temperature"] = goal if abs(delta) <= step else heater["temperature"] + step * (1 if delta > 0 else -1)
            if self.state == "printing":
                self.print_duration += dt
                self.total_duration += dt
                if self.print_seconds > 0:
                    self.progress = min(1.0, self.progress + dt / self.print_seconds)
                if self.progress >= 1.0:
                    self.state = "complete"
                    self.progress = 1.0
                    self.extruder["target"] = 0.0
                    self.heater_bed["target"] = 0.0
            elif self.state == "paused":
                self.total_duration += dt

    # -- prints -------------------------------------------------------------

    def start_print(self, filename):
        with self.lock:
            if not os.path.isfile(os.path.join(self.gcode_dir, filename)):
                raise ApiError(400, "File not found: %s" % filename)
            self.state = "printing"
            self.filename = filename
            self.progress = 0.0
            self.print_duration = 0.0
            self.total_duration = 0.0
            self.message = ""
            self.homed_axes = "xyz"
            if self.extruder["target"] <= 0:
                self.extruder["target"] = 210.0
            if self.heater_bed["target"] <= 0:
                self.heater_bed["target"] = 60.0

    def run_gcode(self, script):
        """Interpret the handful of commands JusPrin sends. Unknown commands are accepted and logged."""
        for raw in script.replace("\r", "\n").split("\n"):
            line = raw.split(";", 1)[0].strip()
            if not line:
                continue
            words = line.split()
            command = words[0].upper()
            params = {}
            for word in words[1:]:
                key, _, value = word.partition("=")
                if value:
                    params[key.upper()] = value
                elif len(word) > 1:
                    params[word[0].upper()] = word[1:]
            with self.lock:
                if command == "G28":
                    self.homed_axes = "xyz"
                elif command in ("M104", "M109"):
                    self.extruder["target"] = float(params.get("S", 0))
                elif command in ("M140", "M190"):
                    self.heater_bed["target"] = float(params.get("S", 0))
                elif command == "M106":
                    self.fan_speed = min(1.0, float(params.get("S", 255)) / 255.0)
                elif command == "M107":
                    self.fan_speed = 0.0
                elif command == "SET_HEATER_TEMPERATURE":
                    heater = self.heater_bed if params.get("HEATER", "").lower() == "heater_bed" else self.extruder
                    heater["target"] = float(params.get("TARGET", 0))
                elif command == "TURN_OFF_HEATERS":
                    self.extruder["target"] = self.heater_bed["target"] = 0.0
                elif command == "PAUSE":
                    if self.state == "printing":
                        self.state = "paused"
                elif command == "RESUME":
                    if self.state == "paused":
                        self.state = "printing"
                elif command == "CANCEL_PRINT":
                    if self.state in ("printing", "paused"):
                        self.state = "cancelled"
                        self.extruder["target"] = self.heater_bed["target"] = 0.0
                elif command == "SDCARD_PRINT_FILE":
                    self.start_print(params.get("FILENAME", "").strip('"'))
                elif command == "M112":
                    self.klippy_state = "shutdown"
                elif command == "FIRMWARE_RESTART":
                    self.klippy_state = "ready"

    # -- control API ----------------------------------------------------------

    def apply(self, changes):
        """Apply a /sim POST body. Unknown keys are an error so typos do not pass silently."""
        with self.lock:
            for key, value in changes.items():
                if key == "reset":
                    if value:
                        self.reset()
                elif key == "state":
                    if value not in PRINT_STATES:
                        raise ApiError(400, "state must be one of %s" % ", ".join(PRINT_STATES))
                    self.state = value
                    if value == "printing" and self.progress >= 1.0:
                        self.progress = 0.0
                elif key == "klippy_state":
                    if value not in KLIPPY_STATES:
                        raise ApiError(400, "klippy_state must be one of %s" % ", ".join(KLIPPY_STATES))
                    self.klippy_state = value
                elif key == "filename":
                    self.filename = str(value)
                elif key == "progress":
                    self.progress = min(1.0, max(0.0, float(value)))
                elif key == "extruder_temp":
                    self.extruder["temperature"] = float(value)
                elif key == "extruder_target":
                    self.extruder["target"] = float(value)
                elif key == "bed_temp":
                    self.heater_bed["temperature"] = float(value)
                elif key == "bed_target":
                    self.heater_bed["target"] = float(value)
                elif key == "fan":
                    self.fan_speed = min(1.0, max(0.0, float(value)))
                elif key == "homed":
                    self.homed_axes = "xyz" if value else ""
                elif key == "message":
                    self.message = str(value)
                elif key == "offline":
                    self.offline = bool(value)
                elif key == "auto":
                    self.auto = bool(value)
                elif key == "lanes":
                    self.database["lane_data"] = dict(value)
                elif key == "clear_log":
                    if value:
                        del self.log[:]
                else:
                    raise ApiError(400, "unknown key %r" % key)

    def snapshot(self):
        with self.lock:
            return {
                "state": self.state, "klippy_state": self.klippy_state, "offline": self.offline, "auto": self.auto,
                "filename": self.filename, "progress": round(self.progress, 4),
                "print_duration": round(self.print_duration, 1), "total_duration": round(self.total_duration, 1),
                "extruder": dict(self.extruder), "heater_bed": dict(self.heater_bed), "fan": self.fan_speed,
                "homed_axes": self.homed_axes, "message": self.message,
                "lanes": self.database.get("lane_data", {}),
            }

    def server_info(self):
        with self.lock:
            return {
                "klippy_connected": self.klippy_state != "disconnected", "klippy_state": self.klippy_state,
                "components": ["database", "file_manager", "klippy_apis", "machine", "history", "octoprint_compat"],
                "failed_components": [], "registered_directories": ["config", "gcodes"], "warnings": [],
                "websocket_count": 0, "moonraker_version": "v0.9.3-fake", "api_version": [1, 5, 0], "api_version_string": "1.5.0",
                "hostname": "fake-moonraker", "machine_name": "Fake Moonraker",
            }

    def printer_info(self):
        with self.lock:
            return {"state": self.klippy_state, "state_message": "Printer is %s" % self.klippy_state,
                    "hostname": "fake-moonraker", "software_version": "v0.12.0-fake", "cpu_info": "fake",
                    "klipper_path": "/fake/klipper", "python_path": "/fake/python",
                    "log_file": "/fake/klippy.log", "config_file": "/fake/printer.cfg"}

    def status_objects(self):
        """Klipper printer objects in Moonraker's /printer/objects/query shape."""
        with self.lock:
            return {
                "webhooks": {"state": self.klippy_state, "state_message": "Printer is %s" % self.klippy_state},
                "print_stats": {
                    "filename": self.filename, "total_duration": self.total_duration, "print_duration": self.print_duration,
                    "filament_used": 0.0, "state": self.state, "message": self.message,
                    "info": {"total_layer": None, "current_layer": None},
                },
                "virtual_sdcard": {
                    "file_path": os.path.join(self.gcode_dir, self.filename) if self.filename else None,
                    "progress": self.progress, "is_active": self.state == "printing", "file_position": 0, "file_size": 0,
                },
                "display_status": {"progress": self.progress, "message": self.message or None},
                "extruder": {"temperature": self.extruder["temperature"], "target": self.extruder["target"],
                             "power": 1.0 if self.extruder["target"] > self.extruder["temperature"] else 0.0},
                "heater_bed": {"temperature": self.heater_bed["temperature"], "target": self.heater_bed["target"],
                               "power": 1.0 if self.heater_bed["target"] > self.heater_bed["temperature"] else 0.0},
                "fan": {"speed": self.fan_speed, "rpm": None},
                "toolhead": {"homed_axes": self.homed_axes, "position": [0.0, 0.0, 0.0, 0.0]},
                "gcode_move": {"speed_factor": 1.0, "extrude_factor": 1.0, "absolute_coordinates": True},
            }

    # -- files ----------------------------------------------------------------

    def save_gcode(self, subdir, filename, data):
        filename = os.path.basename(filename)
        if not filename or ".." in subdir.split("/"):
            raise ApiError(400, "invalid upload path")
        relative = "/".join(part for part in (subdir.strip("/"), filename) if part)
        target = os.path.join(self.gcode_dir, *relative.split("/"))
        os.makedirs(os.path.dirname(target), exist_ok=True)
        with open(target, "wb") as handle:
            handle.write(data)
        return relative

    def list_gcode(self):
        entries = []
        for root, _dirs, files in os.walk(self.gcode_dir):
            for name in files:
                full = os.path.join(root, name)
                entries.append({"path": os.path.relpath(full, self.gcode_dir).replace(os.sep, "/"),
                                "modified": os.path.getmtime(full), "size": os.path.getsize(full), "permissions": "rw"})
        return sorted(entries, key=lambda entry: entry["path"])

    def record(self, method, path, summary):
        entry = {"time": round(time.time(), 3), "method": method, "path": path, "summary": summary}
        with self.lock:
            self.log.append(entry)
        return entry


class ApiError(Exception):
    def __init__(self, code, message):
        super().__init__(message)
        self.code = code


def ws_encode(payload, opcode=1):
    """One unmasked server-to-client websocket frame."""
    data = payload.encode("utf-8") if isinstance(payload, str) else payload
    header = bytes([0x80 | opcode])
    if len(data) < 126:
        header += bytes([len(data)])
    elif len(data) < 65536:
        header += bytes([126]) + struct.pack(">H", len(data))
    else:
        header += bytes([127]) + struct.pack(">Q", len(data))
    return header + data


def ws_read_frame(stream):
    """Read one client frame as (opcode, payload); None once the peer has gone away."""
    head = stream.read(2)
    if len(head) < 2:
        return None
    opcode, masked, length = head[0] & 0x0F, head[1] & 0x80, head[1] & 0x7F
    if length == 126:
        length = struct.unpack(">H", stream.read(2))[0]
    elif length == 127:
        length = struct.unpack(">Q", stream.read(8))[0]
    key = stream.read(4) if masked else b""
    data = stream.read(length) if length else b""
    if masked:
        data = bytes(byte ^ key[index % 4] for index, byte in enumerate(data))
    return opcode, data


def parse_multipart(content_type, body):
    """Split a multipart/form-data body into (fields, files) without the removed cgi module."""
    header = ("Content-Type: %s\r\nMIME-Version: 1.0\r\n\r\n" % content_type).encode("ascii")
    message = email.parser.BytesParser(policy=email.policy.HTTP).parsebytes(header + body)
    fields, files = {}, {}
    for part in message.iter_parts():
        name = part.get_param("name", header="content-disposition")
        payload = part.get_payload(decode=True) or b""
        if part.get_filename():
            files[name] = (part.get_filename(), payload)
        else:
            fields[name] = payload.decode("utf-8", "replace")
    return fields, files


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    printer = None
    quiet = False

    def log_message(self, *_args):
        pass

    def note(self, summary):
        self.noted = True
        entry = self.printer.record(self.command, self.path.split("?", 1)[0], summary)
        if not self.quiet:
            print("%s %-6s %-28s %s" % (time.strftime("%H:%M:%S"), entry["method"], entry["path"], summary), flush=True)

    # -- plumbing -------------------------------------------------------------

    def body(self):
        length = int(self.headers.get("Content-Length") or 0)
        return self.rfile.read(length) if length else b""

    def json_body(self):
        raw = self.body()
        if not raw.strip():
            return {}
        try:
            return json.loads(raw)
        except ValueError:
            raise ApiError(400, "request body is not JSON")

    def reply(self, payload, code=200, content_type="application/json"):
        data = payload if isinstance(payload, bytes) else json.dumps(payload, indent=2).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def dispatch(self):
        url = urlsplit(self.path)
        path, query = url.path.rstrip("/") or "/", parse_qs(url.query, keep_blank_values=True)
        is_sim = path == "/sim" or path.startswith("/sim/")
        if self.printer.offline and not is_sim:
            # An unplugged printer does not answer at all: drop the connection.
            self.close_connection = True
            self.connection.close()
            return
        self.noted = False
        try:
            handler = getattr(self, "route_" + self.command.lower())
            handler(path, query)
        except ApiError as error:
            self.reply({"error": {"code": error.code, "message": str(error)}}, error.code)
            if not is_sim:
                self.note("error %d: %s" % (error.code, error))
        # Every printer request ends up in the log, even ones whose handler has nothing to add.
        if not is_sim and not self.noted:
            self.note("")

    do_GET = do_POST = do_DELETE = dispatch

    # -- GET --------------------------------------------------------------------

    def route_get(self, path, query):
        printer = self.printer
        if path == "/sim":
            state = printer.snapshot()
            state.update({"print_seconds": printer.print_seconds, "gcode_dir": printer.gcode_dir,
                          "files": printer.list_gcode(), "log": printer.log[-50:]})
            self.reply(state)
        elif path == "/sim/log":
            self.reply({"log": list(printer.log)})
        elif path == "/websocket":
            self.serve_websocket()
        elif path == "/server/info":
            self.note("server info (klippy_state=%s)" % printer.klippy_state)
            self.reply({"result": printer.server_info()})
        elif path == "/printer/info":
            self.reply({"result": printer.printer_info()})
        elif path == "/printer/objects/list":
            self.reply({"result": {"objects": sorted(printer.status_objects())}})
        elif path == "/printer/objects/query":
            objects = printer.status_objects()
            wanted = [key for key in query if key in objects] or list(objects)
            self.note("status query: %s" % ", ".join(wanted))
            self.reply({"result": {"eventtime": time.monotonic(), "status": {key: objects[key] for key in wanted}}})
        elif path == "/server/files/roots":
            self.reply({"result": [{"name": "gcodes", "path": printer.gcode_dir, "permissions": "rw"}]})
        elif path == "/server/files/list":
            root = query.get("root", ["gcodes"])[0]
            if root != "gcodes":
                raise ApiError(404, "Root '%s' not found" % root)
            self.reply({"result": printer.list_gcode()})
        elif path.startswith("/server/files/gcodes/"):
            relative = unquote(path[len("/server/files/gcodes/"):])
            target = os.path.join(printer.gcode_dir, *relative.split("/"))
            if ".." in relative.split("/") or not os.path.isfile(target):
                raise ApiError(404, "File not found: %s" % relative)
            with open(target, "rb") as handle:
                self.reply(handle.read(), content_type="application/octet-stream")
        elif path == "/server/database/item":
            namespace = query.get("namespace", [""])[0]
            key = query.get("key", [None])[0]
            if namespace not in printer.database:
                raise ApiError(404, "Namespace '%s' not found" % namespace)
            value = printer.database[namespace]
            if key is not None:
                if key not in value:
                    raise ApiError(404, "Key '%s' in namespace '%s' not found" % (key, namespace))
                value = value[key]
            self.note("database read %s%s" % (namespace, "/" + key if key else ""))
            self.reply({"result": {"namespace": namespace, "key": key, "value": value}})
        else:
            raise ApiError(404, "No route for GET %s" % path)

    # -- POST ---------------------------------------------------------------------

    def route_post(self, path, _query):
        printer = self.printer
        if path == "/sim":
            printer.apply(self.json_body())
            self.reply(printer.snapshot())
        elif path == "/printer/gcode/script":
            script = self.json_body().get("script", "")
            self.note("gcode: %s" % " | ".join(line.strip() for line in script.splitlines() if line.strip()))
            printer.run_gcode(script)
            self.reply({"result": "ok"})
        elif path == "/server/files/upload":
            content_type = self.headers.get("Content-Type", "")
            if not content_type.startswith("multipart/form-data"):
                raise ApiError(400, "expected multipart/form-data")
            fields, files = parse_multipart(content_type, self.body())
            if "file" not in files:
                raise ApiError(400, "missing 'file' part")
            root = fields.get("root", "gcodes")
            if root != "gcodes":
                raise ApiError(400, "Root '%s' is not writable" % root)
            filename, data = files["file"]
            stored = printer.save_gcode(fields.get("path", ""), filename, data)
            extras = {key: value for key, value in fields.items() if key not in ("file", "root", "path")}
            self.note("upload %s (%d bytes)%s" % (stored, len(data), " " + json.dumps(extras, sort_keys=True) if extras else ""))
            start = fields.get("print", "false").lower() == "true"
            if start:
                printer.start_print(stored)
            self.reply({"result": {"item": {"path": stored, "root": "gcodes", "size": len(data), "modified": time.time()},
                                   "print_started": start, "print_queued": False, "action": "create_file"}})
        elif path == "/printer/print/start":
            filename = self.json_body().get("filename", "")
            self.note("print start %s" % filename)
            printer.start_print(filename)
            self.reply({"result": "ok"})
        elif path in ("/printer/print/pause", "/printer/print/resume", "/printer/print/cancel"):
            verb = path.rsplit("/", 1)[1]
            self.note("print %s" % verb)
            printer.run_gcode({"pause": "PAUSE", "resume": "RESUME", "cancel": "CANCEL_PRINT"}[verb])
            self.reply({"result": "ok"})
        elif path in ("/printer/emergency_stop", "/printer/restart", "/printer/firmware_restart"):
            self.note(path.rsplit("/", 1)[1])
            printer.run_gcode("M112" if path.endswith("emergency_stop") else "FIRMWARE_RESTART")
            self.reply({"result": "ok"})
        elif path == "/server/database/item":
            body = self.json_body()
            namespace, key = body.get("namespace", ""), body.get("key")
            if not namespace or key is None:
                raise ApiError(400, "namespace and key are required")
            printer.database.setdefault(namespace, {})[key] = body.get("value")
            self.note("database write %s/%s" % (namespace, key))
            self.reply({"result": {"namespace": namespace, "key": key, "value": body.get("value")}})
        else:
            raise ApiError(404, "No route for POST %s" % path)

    # -- websocket ------------------------------------------------------------------
    # MoonrakerPrinterAgent opens ws://host/websocket after connecting, identifies itself,
    # subscribes to printer objects, and then expects notify_status_update messages. That
    # stream is what keeps the machine "connected" in JusPrin and carries the print state.

    def serve_websocket(self):
        key = self.headers.get("Sec-WebSocket-Key")
        if self.headers.get("Upgrade", "").lower() != "websocket" or not key:
            raise ApiError(400, "expected a websocket upgrade")
        accept = base64.b64encode(hashlib.sha1((key + WS_GUID).encode("ascii")).digest()).decode("ascii")
        self.send_response(101)
        self.send_header("Upgrade", "websocket")
        self.send_header("Connection", "Upgrade")
        self.send_header("Sec-WebSocket-Accept", accept)
        self.end_headers()
        self.close_connection = True
        self.note("websocket connected")
        send_lock = threading.Lock()
        subscribed = []
        stop = threading.Event()

        def send_frame(payload, opcode=1):
            with send_lock:
                self.wfile.write(ws_encode(payload, opcode))
                self.wfile.flush()

        def notifier():
            # Real Moonraker pushes only changes; pushing the subscribed objects every second is a
            # superset that the agent merges the same way, and it doubles as the keep-alive.
            while not stop.wait(1.0):
                try:
                    if self.printer.offline:
                        self.connection.shutdown(socket.SHUT_RDWR)
                        return
                    if subscribed:
                        objects = self.printer.status_objects()
                        send_frame(json.dumps({"jsonrpc": "2.0", "method": "notify_status_update",
                                               "params": [{name: objects[name] for name in subscribed if name in objects}, time.monotonic()]}))
                except OSError:
                    return

        threading.Thread(target=notifier, daemon=True).start()
        try:
            while True:
                frame = ws_read_frame(self.rfile)
                if frame is None or frame[0] == 8:
                    break
                opcode, data = frame
                if opcode == 9:
                    send_frame(data, opcode=10)
                    continue
                if opcode != 1:
                    continue
                try:
                    request = json.loads(data)
                except ValueError:
                    continue
                reply = self.jsonrpc(request, subscribed)
                if reply is not None:
                    send_frame(json.dumps(reply))
        except OSError:
            pass
        finally:
            stop.set()
            self.note("websocket closed")

    def jsonrpc(self, request, subscribed):
        printer = self.printer
        method, params, request_id = request.get("method"), request.get("params") or {}, request.get("id")
        if method == "server.connection.identify":
            result = {"connection_id": threading.get_ident() & 0xFFFF}
        elif method in ("printer.objects.subscribe", "printer.objects.query"):
            objects = printer.status_objects()
            wanted = [name for name in (params.get("objects") or {}) if name in objects] or list(objects)
            if method == "printer.objects.subscribe":
                subscribed[:] = wanted
            self.note("%s: %s" % (method.rsplit(".", 1)[1], ", ".join(wanted)))
            result = {"eventtime": time.monotonic(), "status": {name: objects[name] for name in wanted}}
        elif method == "server.info":
            result = printer.server_info()
        elif method == "printer.info":
            result = printer.printer_info()
        elif method == "printer.gcode.script":
            script = params.get("script", "")
            self.note("gcode: %s" % " | ".join(line.strip() for line in script.splitlines() if line.strip()))
            printer.run_gcode(script)
            result = "ok"
        else:
            return {"jsonrpc": "2.0", "error": {"code": -32601, "message": "Method not found: %s" % method}, "id": request_id}
        if request_id is None:
            return None
        return {"jsonrpc": "2.0", "result": result, "id": request_id}

    # -- DELETE -------------------------------------------------------------------

    def route_delete(self, path, query):
        printer = self.printer
        if path == "/sim/log":
            printer.apply({"clear_log": True})
            self.reply({"log": []})
        elif path == "/server/database/item":
            namespace, key = query.get("namespace", [""])[0], query.get("key", [None])[0]
            value = printer.database.get(namespace, {}).pop(key, None) if key else None
            if value is None:
                raise ApiError(404, "Key '%s' in namespace '%s' not found" % (key, namespace))
            self.note("database delete %s/%s" % (namespace, key))
            self.reply({"result": {"namespace": namespace, "key": key, "value": value}})
        else:
            raise ApiError(404, "No route for DELETE %s" % path)


def serve(printer, host="127.0.0.1", port=DEFAULT_PORT, quiet=False, tick_seconds=0.25):
    """Start the HTTP server and the simulation clock in background threads. Returns the server."""
    handler = type("BoundHandler", (Handler,), {"printer": printer, "quiet": quiet})
    server = ThreadingHTTPServer((host, port), handler)
    server.daemon_threads = True

    def clock():
        last = time.monotonic()
        while not server.stopped:
            time.sleep(tick_seconds)
            now = time.monotonic()
            printer.tick(now - last)
            last = now

    server.stopped = False
    threading.Thread(target=server.serve_forever, daemon=True).start()
    threading.Thread(target=clock, daemon=True).start()
    original_shutdown = server.shutdown

    def shutdown():
        server.stopped = True
        original_shutdown()

    server.shutdown = shutdown
    return server


# -- JusPrin preset installer ---------------------------------------------------------

def default_datadir():
    if sys.platform == "darwin":
        return os.path.expanduser("~/Library/Application Support/JusPrin2")
    if sys.platform.startswith("win"):
        return os.path.join(os.environ.get("APPDATA", os.path.expanduser("~")), "JusPrin2")
    return os.path.join(os.environ.get("XDG_CONFIG_HOME", os.path.expanduser("~/.config")), "JusPrin2")


def install_preset(datadir, name, parent, host, port):
    """Write a user printer preset that points at the simulator, and select it in the app config.

    JusPrin's shell hides the sidebar that opens the Physical Printer dialog, so this is the only
    way to give a printer preset a print host without a dialog. Run it while JusPrin is closed:
    the app rewrites its config file on exit and would overwrite the selection.
    """
    parent_file = os.path.join(datadir, "system", "Custom", "machine", parent + ".json")
    if not os.path.isfile(parent_file):
        raise SystemExit("Parent preset not found: %s\nInstall the Custom printer vendor in JusPrin first "
                         "(the MyKlipper presets), or pass --inherits with an installed system printer preset." % parent_file)
    # A user preset whose version does not parse is silently skipped at load (Preset.cpp), so take
    # the vendor's version the way JusPrin does when it saves a preset, then the parent's.
    version = ""
    for candidate in (os.path.join(datadir, "system", "Custom.json"), parent_file):
        if not version and os.path.isfile(candidate):
            with open(candidate, encoding="utf-8") as handle:
                version = json.load(handle).get("version", "")
    preset = {
        "from": "User", "inherits": parent, "name": name, "printer_settings_id": name, "version": version or "1.0.0.0",
        # The header chip shows printer_model, so give the fake printer its own name there. Process and
        # filament compatibility is keyed on the parent preset, not on the model, so nothing else changes.
        "printer_model": DEFAULT_MODEL_NAME,
        "host_type": "moonraker", "print_host": "%s:%d" % (host, port), "printhost_port": str(port),
        "printhost_apikey": "", "printhost_authorization_type": "key", "printer_agent": "moonraker",
    }
    preset_dir = os.path.join(datadir, "user", "default", "machine")
    os.makedirs(preset_dir, exist_ok=True)
    preset_file = os.path.join(preset_dir, name + ".json")
    with open(preset_file, "w", encoding="utf-8") as handle:
        json.dump(preset, handle, indent=4)
        handle.write("\n")
    config_file = os.path.join(datadir, "JusPrin2.conf")
    selected = False
    if os.path.isfile(config_file):
        with open(config_file, encoding="utf-8") as handle:
            config = json.load(handle)
        config.setdefault("presets", {})["machine"] = name
        with open(config_file, "w", encoding="utf-8") as handle:
            json.dump(config, handle, indent="\t", ensure_ascii=False)
        selected = True
    return preset_file, selected


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--print-seconds", type=float, default=60.0, help="how long a started print takes to reach 100%% (default 60)")
    parser.add_argument("--gcode-dir", help="where uploaded G-code is stored (default: a folder under the temp dir)")
    parser.add_argument("--quiet", action="store_true", help="do not print each request the app makes")
    parser.add_argument("--install-preset", nargs="?", const=DEFAULT_PRESET_NAME, metavar="NAME",
                        help="write a JusPrin printer preset pointing at this simulator, select it, and exit")
    parser.add_argument("--inherits", default=DEFAULT_PARENT_PRESET, help="system printer preset the installed preset inherits (default: %(default)s)")
    parser.add_argument("--datadir", default=default_datadir(), help="JusPrin data directory (default: %(default)s)")
    args = parser.parse_args(argv)

    if args.install_preset:
        preset_file, selected = install_preset(args.datadir, args.install_preset, args.inherits, args.host, args.port)
        print("Wrote %s" % preset_file)
        print("Selected it as the current printer in JusPrin2.conf" if selected else "No JusPrin2.conf found; select the preset in JusPrin's printer menu")
        return

    printer = Printer(print_seconds=args.print_seconds, gcode_dir=args.gcode_dir)
    server = serve(printer, args.host, args.port, quiet=args.quiet)
    print("Fake Moonraker printer listening on http://%s:%d" % (args.host, server.server_port))
    print("  state and log:  curl -s http://%s:%d/sim" % (args.host, server.server_port))
    print("  uploads go to:  %s" % printer.gcode_dir)
    print("  JusPrin preset: python3 %s --install-preset   (once, with JusPrin closed)" % os.path.relpath(__file__))
    try:
        while True:
            time.sleep(3600)
    except KeyboardInterrupt:
        server.shutdown()


if __name__ == "__main__":
    main()
