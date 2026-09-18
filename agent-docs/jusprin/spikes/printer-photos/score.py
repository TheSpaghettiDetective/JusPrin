#!/usr/bin/env python3
"""Score photo runs. Usage: ./score.py RUN [RUN ...]"""
import json, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
CAT = {l.split(" | ")[0] for l in open(f"{HERE}/../printer-naming/catalog.txt").read().splitlines()}
CASES = [json.loads(l) for l in open(f"{HERE}/corpus.jsonl")]

def judge(case, got):
    exp, ids, action = case["expect"], got.get("catalogIds") or [], got.get("action")
    problems = []
    invented = [i for i in ids if i not in CAT]
    if invented: problems.append(f"invented id(s): {invented}")
    missing = [i for i in exp.get("must_include", []) if i not in ids]
    if missing: problems.append(f"missing required: {missing}")
    anyof = exp.get("must_include_any")
    if anyof and not any(i in ids for i in anyof): problems.append(f"none of {anyof}")
    allowed = set(exp.get("must_include", [])) | set(exp.get("allow_extra", [])) | set(anyof or [])
    extra = [i for i in ids if i in CAT and i not in allowed]
    if extra: problems.append(f"out-of-set card(s): {extra}")
    if len(ids) > exp.get("max_cards", 3): problems.append(f"{len(ids)} cards, cap {exp.get('max_cards', 3)}")
    accept = exp.get("accept_actions", ["propose", "ask"])
    if action not in accept: problems.append(f"action {action}, expected one of {accept}")
    question = (got.get("question") or "").strip()
    if exp.get("require_question") and not question: problems.append("no question asked")
    if exp.get("require_question_if_cards") and ids and not question: problems.append("cards shown without a question")
    if exp.get("forbid_unsupported") and action == "unsupported": problems.append("declared unsupported")
    return problems

for run in sys.argv[1:]:
    passed = failed = missing = 0
    lines = []
    for case in CASES:
        path = f"{HERE}/out/{run}/{case['id']}.json"
        if not os.path.exists(path):
            missing += 1; lines.append(f"[MISSING] {case['id']}"); continue
        got = json.load(open(path))
        problems = judge(case, got)
        tag = "PASS" if not problems else "FAIL"
        passed += tag == "PASS"; failed += tag == "FAIL"
        lines.append(f"[{tag}] {case['id']}{' (uncertain truth)' if case.get('uncertain') else ''} {case['photo']} + {case['text']!r}")
        lines.append(f"    -> {got.get('action')} {got.get('catalogIds')}")
        if got.get("question"): lines.append(f"    ?  {got['question']}")
        lines.append(f"    .  {got.get('say','')}")
        lines.append(f"    👁 {got.get('evidence','')}")
        lines += [f"    !  {p}" for p in problems]
    print(f"=== {run}: PASS {passed}  FAIL {failed}  MISSING {missing}")
    print("\n".join(lines)); print()
