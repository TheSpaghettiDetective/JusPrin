#!/usr/bin/env python3
"""Helpers for run.sh's chat mode.

  turn.py case   HISTORY LINE          print a corpus-style case for make_prompt.py
  turn.py render HISTORY LINE ANSWER   show the answer, then record the turn
"""
import json, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))

def load(path):
    with open(path) as fh:
        return json.load(fh)

def case(history_path, line):
    print(json.dumps({"id": "chat", "query": line, "history": load(history_path)}, ensure_ascii=False))

def render(history_path, line, answer_path, raw):
    answer = load(answer_path)
    catalog = {e["catalogId"]: e for e in load(os.path.join(HERE, "catalog.json"))}
    ids = answer.get("catalogIds") or []

    if raw:
        print(json.dumps(answer, indent=1, ensure_ascii=False))
    say, question = answer.get("say", "").strip(), answer.get("question", "").strip()
    print(f"\nassistant> {say}")
    if question:
        print(f"           {question}")
    if answer.get("evidence"):
        print(f"   seen: {answer['evidence']}")
    for i in ids:
        entry = catalog.get(i)
        if entry is None:
            print(f"   !! INVENTED ID, no such printer: {i}")
        else:
            print(f"   [card] {entry['model']:<34} {entry['buildVolume'] or '?':<14} {i}")
    action = answer.get("action")
    if action == "unsupported":
        print("   (not in the list: the panel would point at \"Set it up myself\")")
    print(f"   ({action}, {len(ids)} card{'s' if len(ids) != 1 else ''})")

    # The next turn sees what was said and which cards were on screen.
    shown = f" [cards shown: {', '.join(ids)}]" if ids else ""
    if answer.get("evidence"):
        shown += f" [seen in the photo: {answer['evidence']}]"
    history = load(history_path)
    history += [["user", line], ["assistant", " ".join(p for p in (say, question) if p) + shown]]
    with open(history_path, "w") as fh:
        json.dump(history, fh, ensure_ascii=False)

if __name__ == "__main__":
    if sys.argv[1] == "case":
        case(sys.argv[2], sys.argv[3])
    elif sys.argv[1] == "render":
        render(sys.argv[2], sys.argv[3], sys.argv[4], os.environ.get("RAW") == "1")
    else:
        sys.exit(f"unknown command {sys.argv[1]}")
