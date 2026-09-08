"""Tests for the fake Moonraker printer.

Each test drives the simulator over HTTP the way JusPrin does, using the exact
request shapes from src/slic3r/Utils/Moonraker.cpp (Print button) and
src/slic3r/Utils/MoonrakerPrinterAgent.cpp (printer agent). Run with:

    python3 -m unittest discover -s tests/printer_sim -v
"""

import base64
import http.client
import json
import os
import shutil
import socket
import struct
import tempfile
import time
import unittest
import uuid

import fake_moonraker


class WsClient:
    """The websocket handshake and framing MoonrakerPrinterAgent's boost::beast client performs."""

    def __init__(self, port):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=5)
        key = base64.b64encode(os.urandom(16)).decode()
        self.sock.sendall(("GET /websocket HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                           "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n" % (port, key)).encode())
        self.file = self.sock.makefile("rb")
        self.status_line = self.file.readline()
        self.headers = {}
        while True:
            line = self.file.readline()
            if line in (b"\r\n", b""):
                break
            name, _, value = line.decode().partition(":")
            self.headers[name.strip().lower()] = value.strip()

    def send(self, message):
        data = json.dumps(message).encode()
        mask = os.urandom(4)
        header = bytes([0x81])
        if len(data) < 126:
            header += bytes([0x80 | len(data)])
        else:
            header += bytes([0x80 | 126]) + struct.pack(">H", len(data))
        self.sock.sendall(header + mask + bytes(byte ^ mask[index % 4] for index, byte in enumerate(data)))

    def recv(self):
        head = self.file.read(2)
        if len(head) < 2:
            return None
        opcode, length = head[0] & 0x0F, head[1] & 0x7F
        if length == 126:
            length = struct.unpack(">H", self.file.read(2))[0]
        elif length == 127:
            length = struct.unpack(">Q", self.file.read(8))[0]
        data = self.file.read(length)
        return opcode, (json.loads(data) if opcode == 1 else data)

    def recv_until(self, predicate, timeout=5.0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            frame = self.recv()
            if frame is None:
                return None
            if predicate(frame[1]):
                return frame[1]
        raise AssertionError("no matching websocket message within %.1fs" % timeout)

    def close(self):
        self.sock.close()


class SimulatorClient:
    def __init__(self, port):
        self.port = port

    def request(self, method, path, body=None, headers=None):
        connection = http.client.HTTPConnection("127.0.0.1", self.port, timeout=5)
        try:
            data = body
            request_headers = dict(headers or {})
            if isinstance(body, (dict, list)):
                data = json.dumps(body).encode()
                request_headers["Content-Type"] = "application/json"
            connection.request(method, path, data, request_headers)
            response = connection.getresponse()
            payload = response.read()
            if response.getheader("Content-Type", "").startswith("application/json"):
                payload = json.loads(payload)
            return response.status, payload
        finally:
            connection.close()

    def get(self, path):
        return self.request("GET", path)

    def post(self, path, body=None):
        return self.request("POST", path, body)

    def upload(self, filename, data, fields):
        """Multipart form upload shaped like Http::form_add / form_add_file (libcurl)."""
        boundary = "----curl" + uuid.uuid4().hex
        parts = []
        for name, value in fields.items():
            parts.append(("--%s\r\nContent-Disposition: form-data; name=\"%s\"\r\n\r\n%s\r\n" % (boundary, name, value)).encode())
        parts.append(("--%s\r\nContent-Disposition: form-data; name=\"file\"; filename=\"%s\"\r\n"
                      "Content-Type: application/octet-stream\r\n\r\n" % (boundary, filename)).encode())
        parts.append(data + b"\r\n")
        parts.append(("--%s--\r\n" % boundary).encode())
        body = b"".join(parts)
        return self.request("POST", "/server/files/upload", body,
                            {"Content-Type": "multipart/form-data; boundary=" + boundary, "Content-Length": str(len(body))})


class FakeMoonrakerTests(unittest.TestCase):
    def setUp(self):
        self.gcode_dir = tempfile.mkdtemp(prefix="fake_moonraker_test_")
        self.printer = fake_moonraker.Printer(print_seconds=0.4, gcode_dir=self.gcode_dir)
        self.server = fake_moonraker.serve(self.printer, port=0, quiet=True, tick_seconds=0.02)
        self.client = SimulatorClient(self.server.server_port)

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        shutil.rmtree(self.gcode_dir, ignore_errors=True)

    def wait_for(self, predicate, timeout=3.0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if predicate():
                return True
            time.sleep(0.02)
        return False

    # -- what Moonraker::test() and MoonrakerPrinterAgent::connect_printer() need --------------

    def test_server_info_carries_klippy_state(self):
        status, payload = self.client.get("/server/info")
        self.assertEqual(status, 200)
        self.assertEqual(payload["result"]["klippy_state"], "ready")
        self.assertIn("moonraker_version", payload["result"])
        self.assertEqual(self.printer.log[-1]["path"], "/server/info")

    # -- the Print button: upload, then /printer/print/start with result.item.path ---------------

    def test_print_button_flow(self):
        gcode = b"; sliced by JusPrin\r\nG28\r\nG1 X10 Y10\r\n"
        status, payload = self.client.upload("plate_1.gcode", gcode, {"root": "gcodes", "plateindex": "1"})
        self.assertEqual(status, 200)
        stored = payload["result"]["item"]["path"]
        self.assertEqual(stored, "plate_1.gcode")
        self.assertFalse(payload["result"]["print_started"])
        with open(os.path.join(self.gcode_dir, "plate_1.gcode"), "rb") as handle:
            self.assertEqual(handle.read(), gcode)

        status, payload = self.client.post("/printer/print/start", {"filename": stored})
        self.assertEqual(status, 200)
        self.assertEqual(payload, {"result": "ok"})

        status, snapshot = self.client.get("/sim")
        self.assertEqual(snapshot["state"], "printing")
        self.assertEqual(snapshot["filename"], "plate_1.gcode")
        summaries = [entry["summary"] for entry in snapshot["log"]]
        self.assertIn('upload plate_1.gcode (%d bytes) {"plateindex": "1"}' % len(gcode), summaries)
        self.assertIn("print start plate_1.gcode", summaries)

        # The print runs on its own and finishes without any further request.
        self.assertTrue(self.wait_for(lambda: self.printer.state == "complete"))
        self.assertEqual(self.printer.progress, 1.0)

    def test_upload_with_print_true_starts_immediately(self):
        status, payload = self.client.upload("now.gcode", b"G28\n", {"root": "gcodes", "print": "true"})
        self.assertEqual(status, 200)
        self.assertTrue(payload["result"]["print_started"])
        self.assertEqual(self.printer.state, "printing")

    def test_print_start_rejects_unknown_file(self):
        status, payload = self.client.post("/printer/print/start", {"filename": "never_uploaded.gcode"})
        self.assertEqual(status, 400)
        self.assertIn("File not found", payload["error"]["message"])
        self.assertEqual(self.printer.state, "standby")

    def test_upload_rejects_other_roots(self):
        status, payload = self.client.upload("x.gcode", b"", {"root": "config"})
        self.assertEqual(status, 400)
        self.assertIn("not writable", payload["error"]["message"])

    def test_uploaded_files_can_be_listed_and_fetched(self):
        self.client.upload("a.gcode", b"A", {"root": "gcodes"})
        status, listing = self.client.get("/server/files/list?root=gcodes")
        self.assertEqual([entry["path"] for entry in listing["result"]], ["a.gcode"])
        status, data = self.client.get("/server/files/gcodes/a.gcode")
        self.assertEqual(status, 200)
        self.assertEqual(data, b"A")
        status, _ = self.client.get("/server/files/gcodes/../secret")
        self.assertEqual(status, 404)

    # -- the printer agent: G-code, status query, filament lanes ----------------------------------

    def test_gcode_script_drives_heaters_and_print_state(self):
        for script, check in (
            ("M104 S200", lambda: self.printer.extruder["target"] == 200.0),
            ("M140 S60", lambda: self.printer.heater_bed["target"] == 60.0),
            ("SET_HEATER_TEMPERATURE HEATER=heater_bed TARGET=80", lambda: self.printer.heater_bed["target"] == 80.0),
            ("SET_HEATER_TEMPERATURE HEATER=extruder TARGET=0", lambda: self.printer.extruder["target"] == 0.0),
            ("G28", lambda: self.printer.homed_axes == "xyz"),
            ("M106 S255", lambda: self.printer.fan_speed == 1.0),
            ("M107", lambda: self.printer.fan_speed == 0.0),
            ("SOME_MACRO_JUSPRIN_DOES_NOT_KNOW", lambda: True),
        ):
            status, payload = self.client.post("/printer/gcode/script", {"script": script})
            self.assertEqual(status, 200, script)
            self.assertEqual(payload, {"result": "ok"}, script)
            self.assertTrue(check(), script)
        self.assertEqual(self.printer.log[-1]["summary"], "gcode: SOME_MACRO_JUSPRIN_DOES_NOT_KNOW")

    def test_sdcard_print_file_then_pause_resume_cancel(self):
        self.client.upload("agent.gcode", b"G1\n", {"root": "gcodes", "print": "false"})
        self.printer.print_seconds = 3600
        self.client.post("/printer/gcode/script", {"script": "SDCARD_PRINT_FILE FILENAME=agent.gcode"})
        self.assertEqual(self.printer.state, "printing")
        self.client.post("/printer/gcode/script", {"script": "PAUSE"})
        self.assertEqual(self.printer.state, "paused")
        self.client.post("/printer/gcode/script", {"script": "RESUME"})
        self.assertEqual(self.printer.state, "printing")
        self.client.post("/printer/gcode/script", {"script": "CANCEL_PRINT"})
        self.assertEqual(self.printer.state, "cancelled")
        self.assertEqual(self.printer.extruder["target"], 0.0)

    def test_objects_query_has_the_fields_the_agent_reads(self):
        status, payload = self.client.get("/printer/objects/query?print_stats&virtual_sdcard&extruder&heater_bed&fan")
        self.assertEqual(status, 200)
        objects = payload["result"]["status"]
        self.assertEqual(sorted(objects), ["extruder", "fan", "heater_bed", "print_stats", "virtual_sdcard"])
        self.assertEqual(objects["print_stats"]["state"], "standby")
        for key in ("filename", "total_duration", "print_duration"):
            self.assertIn(key, objects["print_stats"])
        self.assertIn("progress", objects["virtual_sdcard"])
        for heater in ("extruder", "heater_bed"):
            self.assertIn("temperature", objects[heater])
            self.assertIn("target", objects[heater])
        self.assertIn("speed", objects["fan"])
        status, everything = self.client.get("/printer/objects/query")
        self.assertIn("toolhead", everything["result"]["status"])
        status, listing = self.client.get("/printer/objects/list")
        self.assertIn("print_stats", listing["result"]["objects"])

    def test_lane_data_read_write_delete(self):
        status, payload = self.client.get("/server/database/item?namespace=lane_data")
        self.assertEqual(status, 200)
        lanes = payload["result"]["value"]
        self.assertEqual(len(lanes), 4)
        for lane in lanes.values():
            for key in ("lane", "color", "material", "bed_temp", "nozzle_temp"):
                self.assertIn(key, lane)
        status, _ = self.client.post("/server/database/item",
                                     {"namespace": "lane_data", "key": "lane5", "value": {"lane": "5", "material": "ASA", "color": "#123456"}})
        self.assertEqual(status, 200)
        status, payload = self.client.get("/server/database/item?namespace=lane_data&key=lane5")
        self.assertEqual(payload["result"]["value"]["material"], "ASA")
        status, _ = self.client.request("DELETE", "/server/database/item?namespace=lane_data&key=lane5")
        self.assertEqual(status, 200)
        status, _ = self.client.get("/server/database/item?namespace=lane_data&key=lane5")
        self.assertEqual(status, 404)
        status, _ = self.client.get("/server/database/item?namespace=nope")
        self.assertEqual(status, 404)

    # -- the /sim control API used by people and AI agents --------------------------------------------

    def test_control_api_sets_state_and_rejects_typos(self):
        status, snapshot = self.client.post("/sim", {"state": "printing", "filename": "hand_set.gcode", "progress": 0.42,
                                                     "extruder_temp": 205, "bed_target": 60, "auto": False})
        self.assertEqual(status, 200)
        self.assertEqual(snapshot["state"], "printing")
        self.assertEqual(snapshot["progress"], 0.42)
        self.assertEqual(snapshot["extruder"]["temperature"], 205.0)
        status, payload = self.client.get("/printer/objects/query?print_stats&virtual_sdcard")
        self.assertEqual(payload["result"]["status"]["print_stats"]["filename"], "hand_set.gcode")
        self.assertEqual(payload["result"]["status"]["virtual_sdcard"]["progress"], 0.42)

        status, payload = self.client.post("/sim", {"state": "flying"})
        self.assertEqual(status, 400)
        status, payload = self.client.post("/sim", {"progess": 1})
        self.assertEqual(status, 400)
        self.assertIn("unknown key", payload["error"]["message"])

        status, snapshot = self.client.post("/sim", {"reset": True})
        self.assertEqual(snapshot["state"], "standby")
        self.assertTrue(snapshot["auto"])

    def test_auto_false_freezes_the_simulation(self):
        self.client.upload("frozen.gcode", b"G1\n", {"root": "gcodes"})
        self.client.post("/sim", {"auto": False})
        self.client.post("/printer/print/start", {"filename": "frozen.gcode"})
        time.sleep(0.3)
        self.assertEqual(self.printer.state, "printing")
        self.assertEqual(self.printer.progress, 0.0)

    def test_klippy_state_is_reported_by_server_info(self):
        self.client.post("/sim", {"klippy_state": "shutdown"})
        status, payload = self.client.get("/server/info")
        self.assertEqual(payload["result"]["klippy_state"], "shutdown")
        self.client.post("/printer/gcode/script", {"script": "FIRMWARE_RESTART"})
        status, payload = self.client.get("/server/info")
        self.assertEqual(payload["result"]["klippy_state"], "ready")

    def test_offline_drops_printer_requests_but_not_sim_requests(self):
        self.client.post("/sim", {"offline": True})
        with self.assertRaises((http.client.HTTPException, ConnectionError, OSError)):
            self.client.get("/server/info")
        status, snapshot = self.client.get("/sim")
        self.assertEqual(status, 200)
        self.assertTrue(snapshot["offline"])
        self.client.post("/sim", {"offline": False})
        status, _ = self.client.get("/server/info")
        self.assertEqual(status, 200)

    def test_log_can_be_read_and_cleared(self):
        self.client.get("/server/info")
        self.client.get("/server/files/roots")
        self.client.get("/no/such/route")
        self.client.get("/sim")
        status, payload = self.client.get("/sim/log")
        self.assertEqual([entry["path"] for entry in payload["log"]], ["/server/info", "/server/files/roots", "/no/such/route"])
        self.assertEqual(payload["log"][1]["summary"], "")
        self.assertEqual(payload["log"][2]["summary"], "error 404: No route for GET /no/such/route")
        status, payload = self.client.request("DELETE", "/sim/log")
        self.assertEqual(payload, {"log": []})
        self.assertEqual(self.printer.log, [])

    # -- the websocket status stream MoonrakerPrinterAgent::run_status_stream opens -------------

    def test_websocket_identify_subscribe_and_status_notifications(self):
        client = WsClient(self.server.server_port)
        try:
            self.assertIn(b"101", client.status_line)
            self.assertEqual(client.headers["upgrade"], "websocket")
            client.send({"jsonrpc": "2.0", "method": "server.connection.identify", "id": 0,
                         "params": {"client_name": "OrcaSlicer", "version": "1.0.0", "type": "agent", "url": "x"}})
            reply = client.recv_until(lambda message: message.get("id") == 0)
            self.assertIn("connection_id", reply["result"])
            client.send({"jsonrpc": "2.0", "method": "printer.objects.subscribe", "id": 1,
                         "params": {"objects": {"print_stats": None, "virtual_sdcard": None, "extruder": None, "heater_bed": None, "fan": None, "toolhead": None}}})
            reply = client.recv_until(lambda message: message.get("id") == 1)
            self.assertEqual(sorted(reply["result"]["status"]), ["extruder", "fan", "heater_bed", "print_stats", "toolhead", "virtual_sdcard"])
            self.assertEqual(reply["result"]["status"]["print_stats"]["state"], "standby")
            self.assertIn("eventtime", reply["result"])
            # A state change made through the control API arrives as a notification within a second or two.
            self.client.post("/sim", {"state": "printing", "filename": "ws.gcode", "progress": 0.5, "auto": False})
            update = client.recv_until(lambda message: message.get("method") == "notify_status_update"
                                       and message["params"][0].get("print_stats", {}).get("state") == "printing")
            self.assertEqual(update["params"][0]["virtual_sdcard"]["progress"], 0.5)
            self.assertEqual(update["params"][0]["print_stats"]["filename"], "ws.gcode")
            self.assertEqual([entry["summary"] for entry in self.printer.log[:2]],
                             ["websocket connected", "subscribe: print_stats, virtual_sdcard, extruder, heater_bed, fan, toolhead"])
        finally:
            client.close()

    def test_websocket_rejects_unknown_methods(self):
        client = WsClient(self.server.server_port)
        try:
            client.send({"jsonrpc": "2.0", "method": "server.nope", "id": 7})
            reply = client.recv_until(lambda message: message.get("id") == 7)
            self.assertEqual(reply["error"]["code"], -32601)
        finally:
            client.close()

    def test_websocket_closes_when_the_printer_goes_offline(self):
        client = WsClient(self.server.server_port)
        try:
            client.send({"jsonrpc": "2.0", "method": "printer.objects.subscribe", "id": 1, "params": {"objects": {"print_stats": None}}})
            client.recv_until(lambda message: message.get("id") == 1)
            self.client.post("/sim", {"offline": True})
            self.assertIsNone(client.recv_until(lambda message: False))
        finally:
            client.close()

    def test_unknown_routes_are_404_moonraker_errors(self):
        status, payload = self.client.get("/access/oneshot_token")
        self.assertEqual(status, 404)
        self.assertEqual(payload["error"]["code"], 404)


class MultipartParserTests(unittest.TestCase):
    def test_binary_payload_survives(self):
        boundary = "b0undary"
        data = bytes(range(256)) * 3 + b"\r\n\r\n--not-a-boundary\r\n"
        body = (b"--b0undary\r\nContent-Disposition: form-data; name=\"root\"\r\n\r\ngcodes\r\n"
                b"--b0undary\r\nContent-Disposition: form-data; name=\"file\"; filename=\"bin.gcode\"\r\n"
                b"Content-Type: application/octet-stream\r\n\r\n" + data + b"\r\n--b0undary--\r\n")
        fields, files = fake_moonraker.parse_multipart("multipart/form-data; boundary=" + boundary, body)
        self.assertEqual(fields, {"root": "gcodes"})
        self.assertEqual(files["file"], ("bin.gcode", data))


class InstallPresetTests(unittest.TestCase):
    def setUp(self):
        self.datadir = tempfile.mkdtemp(prefix="fake_moonraker_datadir_")
        system_dir = os.path.join(self.datadir, "system", "Custom", "machine")
        os.makedirs(system_dir)
        with open(os.path.join(self.datadir, "system", "Custom.json"), "w") as handle:
            json.dump({"name": "Custom Printer", "version": "02.04.00.01"}, handle)
        with open(os.path.join(system_dir, "MyKlipper 0.4 nozzle.json"), "w") as handle:
            json.dump({"name": "MyKlipper 0.4 nozzle", "inherits": "fdm_klipper_common"}, handle)

    def tearDown(self):
        shutil.rmtree(self.datadir, ignore_errors=True)

    def test_writes_preset_and_selects_it(self):
        config_file = os.path.join(self.datadir, "JusPrin2.conf")
        with open(config_file, "w") as handle:
            json.dump({"presets": {"machine": "MyKlipper 0.4 nozzle", "filaments": None}, "app": {"language": "en"}}, handle)
        preset_file, selected = fake_moonraker.install_preset(self.datadir, "Fake Moonraker 0.4 nozzle", "MyKlipper 0.4 nozzle", "127.0.0.1", 7125)
        self.assertTrue(selected)
        with open(preset_file) as handle:
            preset = json.load(handle)
        self.assertEqual(preset["inherits"], "MyKlipper 0.4 nozzle")
        self.assertEqual(preset["printer_model"], "Fake Moonraker")
        self.assertEqual(preset["host_type"], "moonraker")
        self.assertEqual(preset["print_host"], "127.0.0.1:7125")
        self.assertEqual(preset["printhost_port"], "7125")
        self.assertEqual(preset["printer_agent"], "moonraker")
        self.assertEqual(preset["version"], "02.04.00.01")
        self.assertEqual(preset_file, os.path.join(self.datadir, "user", "default", "machine", "Fake Moonraker 0.4 nozzle.json"))
        with open(config_file) as handle:
            config = json.load(handle)
        self.assertEqual(config["presets"]["machine"], "Fake Moonraker 0.4 nozzle")
        self.assertEqual(config["app"]["language"], "en")

    def test_without_config_file_only_writes_preset(self):
        preset_file, selected = fake_moonraker.install_preset(self.datadir, "Fake", "MyKlipper 0.4 nozzle", "127.0.0.1", 7125)
        self.assertFalse(selected)
        self.assertTrue(os.path.isfile(preset_file))

    def test_missing_parent_preset_is_an_error(self):
        with self.assertRaises(SystemExit):
            fake_moonraker.install_preset(self.datadir, "Fake", "No Such Printer", "127.0.0.1", 7125)


if __name__ == "__main__":
    unittest.main()
