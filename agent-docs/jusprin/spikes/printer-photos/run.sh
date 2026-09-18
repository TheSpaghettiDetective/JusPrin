#!/usr/bin/env bash
# Run the photo corpus against a model.
#   ./run.sh [-m model] [-j N] [-t run-tag] [-p photos-dir] [-i instructions-file]
# Codex tells the model each attachment's path as text, so every photo is
# copied under a random name and attached by that bare name: no filename such
# as "neptune 4 Max", no run tag, no case number reaches the model.
set -u
MODEL="gpt-5.6-terra"; JOBS=4; TAG=""; INSTR=instructions.txt; PHOTOS="$HOME/Downloads/printer-photos"
while [ $# -gt 0 ]; do
  case "$1" in
    -m) MODEL="$2"; shift ;;
    -j) JOBS="$2"; shift ;;
    -t) TAG="$2"; shift ;;
    -p) PHOTOS="$2"; shift ;;
    -i) INSTR="$2"; shift ;;
    -h|--help) sed -n '2,5p' "$0"; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
  shift
done
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"
RUN="$MODEL${TAG:+-$TAG}"
mkdir -p "out/$RUN"
export MODEL RUN PHOTOS INSTR HERE

run_one() {
  local id="$1" dir="out/$RUN"
  [ -s "$dir/$id.json" ] && { echo "skip $id"; return; }
  python3 - "$id" "$dir" <<'PY'
import json, os, secrets, shutil, sys
cid, out = sys.argv[1], sys.argv[2]
case = next(c for c in map(json.loads, open("corpus.jsonl")) if c["id"] == cid)
src = os.path.join(os.environ["PHOTOS"], case["photo"])
name = secrets.token_hex(6) + os.path.splitext(src)[1].lower()
shutil.copyfile(src, os.path.join(out, name))
open(os.path.join(out, f"{cid}.attached"), "w").write(name + "\n")
prompt = [open(os.environ["INSTR"]).read(), open("../printer-naming/catalog.txt").read(), "",
          "The person attached a photo.", f"user: {case['text']}"]
open(os.path.join(out, f"{cid}.prompt.txt"), "w").write("\n".join(prompt))
PY
  local attached; attached=$(cat "$dir/$id.attached")
  (cd "$dir" && codex exec -m "$MODEL" -i "$attached" --skip-git-repo-check --ephemeral --ignore-user-config \
    -s read-only --output-schema "$HERE/schema.json" -o "$id.json" - < "$id.prompt.txt" > "$id.log" 2>&1)
  if [ -s "$dir/$id.json" ]; then echo "done $id"
  else echo "FAILED $id: $(grep -m1 '^ERROR: ' "$dir/$id.log" || echo "see $dir/$id.log")"; fi
}
export -f run_one

python3 -c 'import json
for l in open("corpus.jsonl"): print(json.loads(l)["id"])' | xargs -P "$JOBS" -I{} bash -c 'run_one "$@"' _ {}
echo "results in out/$RUN; score with ./score.py $RUN"
