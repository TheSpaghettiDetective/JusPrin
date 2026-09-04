"""Run the Linux package hooks with host binaries in an AppDir fixture.

This verifies launcher dispatch, arguments and MCP pipes without a GUI. It is
not a Linux ELF dependency audit or proof of an actual mounted AppImage.
"""
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest

from test_bridge_process import BRIDGE, Child, Peer, ROOT, request


@unittest.skipUnless(os.name == "posix" and BRIDGE.is_file(), "Requires POSIX and a built MCP helper")
class LinuxPackageTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="jusprin-package-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.package = self.root / "Kenny's 打印 AppDir"
        self.build = self.root / "build output"
        self.build.mkdir()
        for directory in ("bin", "libexec", "lib/orca-runtime"):
            (self.package / directory).mkdir(parents=True, exist_ok=True)
        self.main = self.build / "orca-slicer"
        self.main.write_text("fixture")
        shutil.copy2(BRIDGE, self.build / "jusprin-mcp")
        self.source = (ROOT / "src/dev-utils/platform/unix/build_linux_image.sh.in").read_text()
        hook = re.search(r'    if \[ -n "@SLIC3R_PACKAGE_EXTRA_SCRIPT@" \]; then\n.*?\n    fi',
                         self.source, re.DOTALL)
        self.assertIsNotNone(hook, "Packaging must invoke the extra-binary hook")
        self.hook = hook.group().replace("@SLIC3R_PACKAGE_EXTRA_SCRIPT@", str(
            ROOT / "src/slic3r/GUI/JusPrin/Mcp/Bridge/package_linux.sh"))
        self.environment = dict(os.environ, ORIGINAL_BINARY_LOCATION=str(self.main), APPDIR=str(self.package))
        self.run_package_hook(check=True)

        # Generate exactly the launcher heredoc used by the upstream packager,
        # then perform the same rename that build_appimage.sh makes.
        begin = self.source.index("cat << EOF >@SLIC3R_APP_CMD@")
        end = self.source.index("\nEOF", begin) + len("\nEOF")
        generate = self.source[begin:end].replace("@SLIC3R_APP_CMD@", "orca-slicer")
        subprocess.run(["bash"], input=generate, text=True, cwd=self.package, check=True)
        self.launcher = self.package / "AppRun"
        (self.package / "orca-slicer").rename(self.launcher)
        self.launcher.chmod(0o755)
        self.discovery = self.root / "Kenny's data; $literal 打印.json"

    def run_package_hook(self, check=False):
        return subprocess.run(["bash", "-e", "-c", self.hook], env=self.environment,
                              capture_output=True, text=True, check=check)

    def child(self):
        child = Child(self.discovery, env={"APPDIR": str(self.package)},
                      command=[str(self.launcher), "--mcp-bridge"])
        self.addCleanup(lambda: self.assertEqual(child.stop()[0], 0))
        return child

    def test_packaged_helper_handshake_and_offline_without_gui_runtime(self):
        # No GUI executable/environment wrapper is installed. The bridge must
        # not invoke graphics or WebKit checks on this route.
        child = self.child()
        self.assertEqual(child.initialize()["result"]["protocolVersion"], "2025-06-18")
        child.send(request(1, "tools/list"))
        self.assertEqual({tool["name"] for tool in child.receive()["result"]["tools"]},
                         {"workspace_inspect", "settings_search", "settings_get", "settings_preview_patch", "settings_apply_patch"})
        child.send(request(2, "tools/call", {"name": "workspace_inspect", "arguments": {}}))
        self.assertEqual(child.receive()["result"]["structuredContent"]["error"]["code"],
                         "workspace_unavailable")

    def test_packaged_helper_follows_discovery_then_reports_closed_app(self):
        child = self.child()
        child.initialize()
        peer = Peer(self.discovery)
        try:
            child.send(request(1, "tools/call", {"name": "workspace_inspect", "arguments": {}}))
            self.assertEqual(child.receive()["result"]["structuredContent"]["peer"], "live")
        finally:
            peer.stop()
        self.discovery.unlink()
        child.send(request(2, "tools/call", {"name": "workspace_inspect", "arguments": {}}))
        self.assertEqual(child.receive()["result"]["structuredContent"]["error"]["code"],
                         "workspace_unavailable")

    def test_ordinary_launch_keeps_upstream_gui_route_and_literal_arguments(self):
        stub = self.root / "gui_fixture.py"
        stub.write_text("import json, os, sys\nprint(json.dumps({'args':sys.argv[1:], 'locale':os.environ.get('LC_ALL')}))\n")
        environment_wrapper = self.package / "libexec/orca-slicer-env"
        environment_wrapper.write_text("#!/bin/sh\nexec " + shlex.quote(sys.executable) + " " +
                                       shlex.quote(str(stub)) + ' "$@"\n')
        environment_wrapper.chmod(0o755)
        arguments = ["Kenny's plate 打印.3mf", "; $literal", "", "--mcp-bridge"]
        result = subprocess.run([str(self.launcher), *arguments], capture_output=True, text=True,
                                env=dict(self.environment, XDG_SESSION_TYPE="x11"), timeout=5, check=True)
        value = json.loads(result.stdout)
        self.assertEqual(value["args"], [str(self.package / "bin/orca-slicer"), *arguments])
        self.assertEqual(value["locale"], "C")

    def test_missing_helper_fails_packaging(self):
        (self.build / "jusprin-mcp").unlink()
        result = self.run_package_hook()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("jusprin-mcp", result.stderr)

    @unittest.skipUnless(sys.platform.startswith("linux") and os.environ.get("JUSPRIN_TEST_APPIMAGETOOL"),
                         "Requires Linux and JUSPRIN_TEST_APPIMAGETOOL")
    def test_compressed_appimage_stdio_live_and_offline(self):
        # A real Type 2 AppImage containing the production helper/launcher,
        # not a full Orca release image. Extraction mode requires no FUSE mount.
        (self.package / "jusprin-mcp-test.desktop").write_text(
            "[Desktop Entry]\nType=Application\nName=JusPrin MCP transport fixture\n"
            "Exec=AppRun\nIcon=jusprin-mcp-test\nCategories=Utility;\n")
        (self.package / "jusprin-mcp-test.svg").write_text(
            '<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32">'
            '<rect width="32" height="32" fill="#345"/></svg>')
        image = self.root / "Kenny's installed 打印.AppImage"
        built = subprocess.run([os.environ["JUSPRIN_TEST_APPIMAGETOOL"], "--appimage-extract-and-run",
                                "--mksquashfs-opt", "-processors", "--mksquashfs-opt", "2",
                                str(self.package), str(image)], capture_output=True, text=True, timeout=90)
        self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
        child = Child(self.discovery, env={"APPIMAGE_EXTRACT_AND_RUN": "1"},
                      command=[str(image), "--mcp-bridge"])
        self.addCleanup(lambda: self.assertEqual(child.stop()[0], 0))
        self.assertEqual(child.initialize()["result"]["protocolVersion"], "2025-06-18")
        child.send(request(1, "tools/list"))
        self.assertEqual({tool["name"] for tool in child.receive()["result"]["tools"]},
                         {"workspace_inspect", "settings_search", "settings_get", "settings_preview_patch", "settings_apply_patch"})
        child.send(request(2, "tools/call", {"name": "workspace_inspect", "arguments": {}}))
        self.assertEqual(child.receive()["result"]["structuredContent"]["error"]["code"],
                         "workspace_unavailable")
        peer = Peer(self.discovery)
        try:
            child.send(request(3, "tools/call", {"name": "workspace_inspect", "arguments": {}}))
            self.assertEqual(child.receive()["result"]["structuredContent"]["peer"], "live")
        finally:
            peer.stop()
        self.discovery.unlink()
        child.send(request(4, "tools/call", {"name": "workspace_inspect", "arguments": {}}))
        self.assertEqual(child.receive()["result"]["structuredContent"]["error"]["code"],
                         "workspace_unavailable")


if __name__ == "__main__":
    unittest.main()
