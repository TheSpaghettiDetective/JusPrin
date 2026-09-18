#!/usr/bin/env python3
"""Score a run against the corpus. Usage: ./score.py [model]"""
import json, os, sys

MODEL = sys.argv[1] if len(sys.argv) > 1 else "gpt-5.6-terra"
HERE = os.path.dirname(os.path.abspath(__file__))
CAT = {e["catalogId"] for e in json.load(open(f"{HERE}/catalog.json"))}

rows = []
for line in open(f"{HERE}/corpus.jsonl"):
    case = json.loads(line)
    exp = case["expect"]
    path = f"{HERE}/out/{MODEL}/{case['id']}.json"
    if not os.path.exists(path):
        rows.append((case, None, "MISSING", ["no result file"])); continue
    try:
        got = json.load(open(path))
    except Exception as err:
        rows.append((case, None, "FAIL", [f"unparseable result: {err}"])); continue

    ids = got.get("catalogIds") or []
    action = got.get("action")
    problems = []

    invented = [i for i in ids if i not in CAT]
    if invented: problems.append(f"invented id(s): {invented}")

    must = exp.get("must_include") or []
    missing = [i for i in must if i not in ids]
    if missing: problems.append(f"missing required: {missing}")

    allowed = set(must) | set(exp.get("allow_extra") or [])
    if allowed:
        extra = [i for i in ids if i in CAT and i not in allowed]
        if extra: problems.append(f"out-of-set card(s): {extra}")

    forbidden = exp.get("must_not_include") or []
    hit = [i for i in ids if i in forbidden]
    if hit: problems.append(f"showed forbidden: {hit}")

    cap = exp.get("max_cards")
    if cap is not None and len(ids) > cap:
        problems.append(f"{len(ids)} cards, cap {cap}")

    want = exp["action"]
    if want == "unsupported":
        if action == "unsupported":
            pass
        elif action == "ask" and not invented and all(i in allowed for i in ids):
            problems.append("SOFT: asked instead of saying it is not in the list")
        else:
            problems.append(f"action {action}, expected unsupported")
    elif exp.get("require_question"):
        if action not in ("propose", "ask"):
            problems.append(f"action {action}, expected a shortlist or a question")
        if not (got.get("question") or "").strip():
            problems.append("no question asked")
    else:
        # "ask" still draws the candidate cards, so what matters is which cards
        # were shown, not which of the two labels the model chose.
        if want == "propose" and action not in ("propose", "ask"):
            problems.append(f"action {action}, expected a card")
        if want == "ask" and action == "propose" and ids:
            problems.append("proposed cards where a question was expected")

    if exp.get("forbid_unsupported") and action == "unsupported":
        problems.append("declared unsupported when the printer IS in the list")

    if want == "ask" and not must and not exp.get("allow_extra") and ids and case["shape"] == "junk":
        problems.append(f"showed {len(ids)} card(s) for junk input")

    hard = [p for p in problems if not p.startswith("SOFT")]
    verdict = "PASS" if not problems else ("SOFT" if not hard else "FAIL")
    rows.append((case, got, verdict, problems))

by_shape = {}
for case, got, verdict, problems in rows:
    by_shape.setdefault(case["shape"], []).append(verdict)

print(f"model: {MODEL}   cases: {len(rows)}")
counts = {}
for _, _, v, _ in rows: counts[v] = counts.get(v, 0) + 1
print("overall:", "  ".join(f"{k} {v}" for k, v in sorted(counts.items())))
print()
print(f"{'shape':20} {'pass':>5} {'soft':>5} {'fail':>5}")
for shape, vs in by_shape.items():
    print(f"{shape:20} {vs.count('PASS'):>5} {vs.count('SOFT'):>5} {vs.count('FAIL')+vs.count('MISSING'):>5}")
print()
for case, got, verdict, problems in rows:
    if verdict == "PASS": continue
    print(f"[{verdict}] {case['id']} ({case['shape']}) {case['query']!r}")
    if got:
        print(f"    -> {got.get('action')} {got.get('catalogIds')}")
        if got.get("question"): print(f"    ?  {got['question']}")
        if got.get("say"): print(f"    .  {got['say']}")
    for p in problems: print(f"    !  {p}")
    print()
