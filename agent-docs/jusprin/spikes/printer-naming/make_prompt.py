#!/usr/bin/env python3
"""Compose the single prompt sent to the model for one corpus case."""
import json, os, sys
case = json.loads(sys.argv[1])
parts = [open(os.environ.get("INSTRUCTIONS", "instructions.txt")).read(), open("catalog.txt").read(), ""]
if case.get("history"):
    parts.append("CONVERSATION SO FAR:")
    for who, text in case["history"]:
        parts.append(f"{who}: {text}")
    parts.append("")
parts.append(f"user: {case['query']}")
sys.stdout.write("\n".join(parts))
