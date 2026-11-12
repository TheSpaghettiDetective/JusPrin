#!/usr/bin/env python3
"""Checks the app guide against the running app and takes its pictures.

guide_checks.json names, for each page of docs/guide, the shell harness runs
that walk the screens the page describes. Each run drives the real app in a
data folder of its own, asserts what it finds (HARNESS CHECK lines) and writes
pictures of it. This script runs them, fails when a run fails, and copies the
pictures a page uses into docs/guide/pictures/<page>/, so a page's steps and
its pictures are both taken from the app as built.

    tests/shell/run_guide_checks.py                  every page
    tests/shell/run_guide_checks.py --area print-settings
    tests/shell/run_guide_checks.py --list           what would run; runs nothing

The harness is the newest one under build/ (the shell_integration_harness
target), or JUSPRIN_SHELL_HARNESS. A run opens the app's window on this
computer's screen and takes real clicks: leave the screen unlocked and the
computer alone while it runs, about a minute a page. A run that fails is tried
once more before it counts. On a Mac grant Screen Recording to the program that starts this script (and to
the harness, if macOS asks), or the whole-window pictures come out black. A
black picture is refused, never copied. Pictures the app draws by itself, the
AI panel's and the plates panel's, need neither.
--keep DIR keeps every run's log and all the pictures it wrote. The pictures
step needs Pillow (pip install pillow).
"""
import argparse
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import time

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
CHECKS = json.loads((HERE / "guide_checks.json").read_text(encoding="utf-8"))
PICTURES = ROOT / "docs/guide/pictures"
HARNESS_NAMES = ("JusPrinShellHarness", "shell_integration_harness", "shell_integration_harness.exe")


def harness():
    named = os.environ.get("JUSPRIN_SHELL_HARNESS")
    if named:
        return pathlib.Path(named)
    built = [path for name in HARNESS_NAMES for path in (ROOT / "build").rglob(name) if path.is_file()]
    if not built:
        sys.exit("the shell harness is not built: build the shell_integration_harness target or set JUSPRIN_SHELL_HARNESS")
    return max(built, key=lambda path: path.stat().st_mtime)


def run(binary, entry, out, log):
    """One harness run. Returns whether the page's part of it passed, what
    failed in that part, how many checks passed, and what failed elsewhere.

    An entry with "checks" depends on those named checks alone: the run is
    stopped once they have passed, and a failure elsewhere in it is reported
    apart, since it is not about the page. Without "checks" the whole run
    has to pass. The harness gives up by itself after 900 s."""
    args = [str(out) if arg == "{dir}" else arg for arg in entry["args"]]
    env = dict(os.environ, **{name: str(out) if value == "{dir}" else value for name, value in entry.get("env", {}).items()})
    needed = set(entry.get("checks", []))
    if needed:
        # A run stopped early never removes its data folder, which the harness makes in the
        # temporary folder: put that beside the run, to remove it here. Only for these runs:
        # the printer menu's run takes ten minutes instead of six seconds with it moved.
        env["TMPDIR"] = str(out / "data")
        (out / "data").mkdir(parents=True, exist_ok=True)
    passed, failed, lines = set(), [], []
    process = subprocess.Popen([str(binary), *args], env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               text=True, errors="replace")
    for line in process.stdout:
        lines.append(line)
        check = re.search(r"HARNESS CHECK (\S+) (PASS|FAIL)", line)
        if check:
            (passed.add if check.group(2) == "PASS" else failed.append)(check.group(1))
        if needed and (needed <= passed or needed & set(failed)):
            process.terminate()
            break
    process.wait()
    text = "".join(lines)
    log.write_text(text, encoding="utf-8")
    shutil.rmtree(out / "data", ignore_errors=True)
    if needed:
        missing = sorted(needed - passed)
        return not missing, [f"{name} did not pass" for name in missing], len(passed), [name for name in failed if name not in needed]
    return "HARNESS RESULT PASS" in text, failed + re.findall(r"HARNESS ERROR (.+)", text), len(passed), []


def copy_pictures(area, entry, out):
    """Copies the run's pictures the page uses: "name.png": "the run's file",
    or {"from": "the run's file", "crop": [left, top, right, bottom]} to keep
    part of it, in the file's own pixels. A picture is kept at twice the
    screen's size, the size the app's web views are captured at, and the guide
    window shows it at half: "atScreenSize": true marks a capture drawn at the
    screen's own size, which is doubled. Returns the names it could not."""
    from PIL import Image, ImageChops  # only this step needs it

    missing = []
    for name, wanted in entry.get("pictures", {}).items():
        wanted = {"from": wanted} if isinstance(wanted, str) else wanted
        made = out / wanted["from"]
        if not made.is_file() or made.stat().st_size == 0:
            missing.append(f"{name} (the run wrote no {wanted['from']})")
            continue
        picture = Image.open(made).convert("RGB")
        if "crop" in wanted:
            picture = picture.crop(tuple(wanted["crop"]))
        if wanted.get("atScreenSize"):
            picture = picture.resize((picture.width * 2, picture.height * 2), Image.NEAREST)
        # One flat colour is a capture the screen refused, not a picture of the app.
        if picture.getextrema() == tuple((low, low) for low, _ in picture.getextrema()):
            missing.append(f"{name} ({wanted['from']} is blank: see this script's note on Screen Recording)")
            continue
        target = PICTURES / area / name
        target.parent.mkdir(parents=True, exist_ok=True)
        # An unchanged picture is left alone, so a run that changes nothing changes no file.
        if target.is_file() and not ImageChops.difference(Image.open(target).convert("RGB"), picture).getbbox() \
                and Image.open(target).size == picture.size:
            continue
        picture.save(target, optimize=True)
    return missing


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--area", choices=sorted(CHECKS), help="check only this page")
    parser.add_argument("--only", metavar="TEXT", help="only the runs whose harness arguments contain TEXT")
    parser.add_argument("--list", action="store_true", help="print what would run and stop")
    parser.add_argument("--keep", metavar="DIR", help="keep each run's log and every picture it wrote here")
    args = parser.parse_args()
    areas = [args.area] if args.area else sorted(CHECKS)
    if args.list:
        for area in areas:
            for entry in CHECKS[area]:
                print(f"{area}: {' '.join(entry['args']) or '(default run)'} -> {', '.join(entry['covers'])}")
        return
    binary = harness()
    keep = pathlib.Path(args.keep) if args.keep else pathlib.Path(tempfile.mkdtemp(prefix="jusprin-guide-"))
    print(f"harness {binary}\nlogs and pictures in {keep}")
    problems = 0
    for area in areas:
        for index, entry in enumerate(CHECKS[area], 1):
            label = " ".join(arg for arg in entry["args"] if arg != "{dir}") or "(default run)"
            if args.only and args.only not in label:
                continue
            out = keep / area / f"{index}-{label.strip('-() ').replace(' ', '-') or 'default'}"
            out.mkdir(parents=True, exist_ok=True)
            started = time.time()
            passed, failed, checks, elsewhere = run(binary, entry, out, out / "harness.log")
            # A run takes real clicks, and loses them when another window comes to the front
            # partway: one that failed gets a second go before it counts.
            again = not passed
            if again:
                passed, failed, checks, elsewhere = run(binary, entry, out, out / "harness-second-try.log")
            missing = copy_pictures(area, entry, out) if passed else []
            verdict = ("pass on the second try" if again else "pass") if passed and not missing else "FAIL"
            print(f"{area}: {label}: {verdict} ({checks} checks, {int(time.time() - started)} s) covers {', '.join(entry['covers'])}")
            for line in failed[:10]:
                print(f"    failed: {line}")
            if elsewhere:
                print(f"    also failed in this run, outside this page: {', '.join(elsewhere)}")
            for line in missing:
                print(f"    no picture: {line}")
            sys.stdout.flush()
            problems += verdict == "FAIL"
    print(f"{problems} of the runs failed" if problems else "every run passed")
    sys.exit(1 if problems else 0)


if __name__ == "__main__":
    main()
