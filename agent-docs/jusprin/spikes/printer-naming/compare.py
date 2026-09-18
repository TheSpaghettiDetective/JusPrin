#!/usr/bin/env python3
"""Show where repeat runs of the same prompt disagree. Usage: ./compare.py runA runB ..."""
import json, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
runs = sys.argv[1:]
cases = [json.loads(l) for l in open(f"{HERE}/corpus.jsonl")]
unstable = 0
for c in cases:
    answers = []
    for r in runs:
        p = f"{HERE}/out/{r}/{c['id']}.json"
        g = json.load(open(p)) if os.path.exists(p) else {}
        answers.append((g.get("action"), tuple(sorted(g.get("catalogIds") or []))))
    if len(set(answers)) > 1:
        unstable += 1
        print(f"{c['id']} {c['query']!r}")
        for r, a in zip(runs, answers):
            print(f"    {r:24} {a[0]} {list(a[1])}")
print(f"\n{len(cases)-unstable}/{len(cases)} identical across {len(runs)} runs")
