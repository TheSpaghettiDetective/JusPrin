#!/usr/bin/env python3
"""Checks the app guide (docs/guide) from end to end, and rebuilds what is
built from it. Run it after editing a page, and before a release.

  1. Walks each page's screens in the app as built and takes its pictures
     (tests/shell/run_guide_checks.py). Needs the shell_integration_harness
     and jusprin_web targets built from this checkout, and this computer's
     screen.
  2. Rebuilds the skill AI reads from each page, and checks that every label
     a page quotes is in the source, that every section has a run in step 1,
     that every picture is one step 1 took, and that every page has questions
     for step 3 (AgentUI's guide.test.ts).
  3. With --ask-ai: asks the model each page's questions and has a judge read
     the answers (tests/project_prompt/run_guide_tests.py), on the Claude
     subscription unless told otherwise after `--`.

    scripts/check_guide.py
    scripts/check_guide.py --area print-settings --ask-ai
    scripts/check_guide.py --skip-app             steps 2 and 3 only
    scripts/check_guide.py --ask-ai -- --runs 1   options after -- go to step 3

Exits 0 when every step it ran passed. When a run of all three steps for
every page passes, it writes the app's version (Updates/Version.hpp) to
docs/guide/checked-version.txt. Commit that file with the pages: the release
workflow refuses a version the guide was not checked for. The file says the
check passed for that version, not on which commit.
"""
import argparse
import os
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
AGENT_UI = ROOT / "src/slic3r/GUI/JusPrin/AgentUI"
RECORD = ROOT / "docs/guide/checked-version.txt"


def step(title, command, cwd=ROOT, env=None):
    print(f"\n== {title}\n   {' '.join(map(str, command))}", flush=True)
    return subprocess.run(command, cwd=cwd, env=env).returncode == 0


def main():
    parser = argparse.ArgumentParser(description="Checks the app guide and rebuilds what is built from it.")
    parser.add_argument("--area", help="one page, by its file name without .md")
    parser.add_argument("--skip-app", action="store_true", help="do not run the app: no walk, no new pictures")
    parser.add_argument("--ask-ai", action="store_true", help="also ask the model each page's questions")
    parser.add_argument("rest", nargs="*", help="after --: options for the questions run")
    args = parser.parse_args()
    area = ["--area", args.area] if args.area else []
    npx = shutil.which("npx")
    if npx is None:
        sys.exit("npx is not on PATH: the page checks need node")

    results = {}
    if not args.skip_app:
        results["the app walk and pictures"] = step(
            "1/3 walking the pages in the app and taking their pictures",
            [sys.executable, ROOT / "tests/shell/run_guide_checks.py", *area])
    results["skills, labels, sections, pictures and questions"] = step(
        "2/3 rebuilding the skills AI reads and checking the pages",
        [npx, "vitest", "run", "src/guide.test.ts"], cwd=AGENT_UI,
        env=dict(os.environ, JUSPRIN_UPDATE_GUIDE_SKILLS="1"))
    if args.ask_ai:
        options = args.rest or ["--backend", "claude-cli", "--judge", "claude-cli"]
        # The questions run exits 2 when it has no baseline to compare with, which it never has.
        print("\n== 3/3 asking the model each page's questions", flush=True)
        asked = subprocess.run([sys.executable, ROOT / "tests/project_prompt/run_guide_tests.py", *area, "--must-pass", "1", *options],
                               cwd=ROOT)
        results["the model's answers"] = asked.returncode == 0

    print()
    for name, passed in results.items():
        print(f"{'pass' if passed else 'FAIL'}: {name}")
    if not all(results.values()):
        sys.exit(1)
    if args.skip_app or args.area or not args.ask_ai:
        print(f"\n{RECORD.relative_to(ROOT)} is unchanged: only a run of all three steps for every page counts for a release.")
        sys.exit(0)
    version = subprocess.run(["node", ROOT / "src/slic3r/GUI/JusPrin/Packaging/release.mjs", "version",
                              ROOT / "src/slic3r/GUI/JusPrin/Updates/Version.hpp"],
                             capture_output=True, text=True, check=True).stdout.strip()
    RECORD.write_text(version + "\n")
    print(f"\nWrote {version} to {RECORD.relative_to(ROOT)}. Commit it with the pages, pictures and skills.")


if __name__ == "__main__":
    main()
