#!/usr/bin/env bash
# Talk to the printer assistant yourself, or run the whole corpus.
#
#   ./run.sh [-m model] [-i instructions-file]                 chat
#   ./run.sh --batch [-m model] [-i instructions-file] [-j N]  corpus
#
# Chat keeps the conversation across turns, so "no wait, the S1" works.
# In chat: /photo PATH [words] attaches a photo, /reset starts over,
# /raw toggles the model's JSON, /quit leaves.
set -u
MODEL="gpt-5.6-terra"
INSTRUCTIONS=""
JOBS=6
MODE=chat
while [ $# -gt 0 ]; do
  case "$1" in
    --batch) MODE=batch ;;
    -m) MODEL="$2"; shift ;;
    -i) INSTRUCTIONS="$2"; shift ;;
    -j) JOBS="$2"; shift ;;
    -h|--help) sed -n '2,9p' "$0"; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
  shift
done
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"
# Chat defaults to the photo-aware prompt, which is v2 plus a section on
# reading photos; the corpus defaults to v2, which its results were scored on.
SCHEMA=schema.json
if [ -z "$INSTRUCTIONS" ]; then
  if [ "$MODE" = chat ]; then INSTRUCTIONS=../printer-photos/instructions.txt; SCHEMA=../printer-photos/schema.json
  else INSTRUCTIONS=instructions.v2.txt; fi
fi
[ -f "$INSTRUCTIONS" ] || { echo "no instructions file: $INSTRUCTIONS" >&2; exit 2; }
export MODEL INSTRUCTIONS SCHEMA HERE

ask_model() {  # ask_model PROMPT-FILE ANSWER-FILE LOG-FILE [IMAGE]
  # Codex tells the model an attachment's path as text, so an image is
  # attached by its bare name from inside its own folder.
  local image=() dir="." schema="$SCHEMA"
  case "$schema" in /*) ;; *) schema="$HERE/$schema" ;; esac
  if [ -n "${4:-}" ]; then dir="$(dirname "$4")"; image=(-i "$(basename "$4")"); fi
  (cd "$dir" && codex exec -m "$MODEL" ${image[@]+"${image[@]}"} --skip-git-repo-check --ephemeral --ignore-user-config \
    -s read-only --output-schema "$schema" -o "$2" - < "$1" > "$3" 2>&1)
}
export -f ask_model

batch() {
  TAG="${INSTRUCTIONS%.txt}"; TAG="${TAG#instructions}"; TAG="${TAG#.}"
  RUN="$MODEL${TAG:+-$TAG}"
  mkdir -p "out/$RUN"
  export RUN

  run_one() {
    local id="$1"
    [ -s "out/$RUN/$id.json" ] && { echo "skip $id"; return; }
    local line
    line=$(python3 -c 'import json,sys
for l in open("corpus.jsonl"):
    if json.loads(l)["id"] == sys.argv[1]: print(l, end=""); break' "$id")
    python3 make_prompt.py "$line" > "out/$RUN/$id.prompt.txt"
    ask_model "out/$RUN/$id.prompt.txt" "out/$RUN/$id.json" "out/$RUN/$id.log"
    if [ -s "out/$RUN/$id.json" ]; then echo "done $id"
    else echo "FAILED $id: $(grep -m1 '^ERROR: ' "out/$RUN/$id.log" || echo "see out/$RUN/$id.log")"; fi
  }
  export -f run_one

  python3 -c 'import json
for l in open("corpus.jsonl"): print(json.loads(l)["id"])' \
    | xargs -P "$JOBS" -I{} bash -c 'run_one "$@"' _ {}
  echo "results in out/$RUN; score with ./score.py $RUN"
}

chat() {
  local session history turn=0 line photo words path
  session=$(mktemp -d "${TMPDIR:-/tmp}/printer-chat.XXXXXX")
  history="$session/history.json"
  echo '[]' > "$history"
  export RAW=0
  set -o history
  echo "Printer assistant: $MODEL, $INSTRUCTIONS"
  echo "Say what printer you have, or /photo PATH [words] to attach a picture."
  echo "/reset starts over, /raw shows the JSON, /quit leaves."
  echo "Transcript and prompts are kept in $session"

  while IFS= read -r -e -p $'\nyou> ' line; do
    case "$line" in
      "") continue ;;
      /quit|/exit) break ;;
      /reset) echo '[]' > "$history"; echo "(new conversation)"; continue ;;
      /raw) RAW=$((1 - RAW)); echo "(raw JSON $([ "$RAW" = 1 ] && echo on || echo off))"; continue ;;
    esac
    history -s "$line"
    turn=$((turn + 1))
    photo=""
    case "$line" in
      /photo\ *)
        # The path may contain spaces: take the longest prefix that is a file.
        words="${line#/photo }"; path="$words"
        while [ -n "$path" ] && [ ! -f "${path/#\~/$HOME}" ]; do
          case "$path" in *\ *) path="${path% *}" ;; *) path="" ;; esac
        done
        [ -z "$path" ] && { echo "!! no such file in: ${line#/photo }"; turn=$((turn - 1)); continue; }
        words="${words#"$path"}"; words="${words# }"
        # A random name, so the filename cannot tell the model the answer.
        photo="$session/$(openssl rand -hex 6).${path##*.}"
        cp "${path/#\~/$HOME}" "$photo"
        line="(attached a photo) ${words:-this is my printer}"
        ;;
    esac
    python3 make_prompt.py "$(python3 turn.py case "$history" "$line")" > "$session/$turn.prompt.txt"
    echo "(asking $MODEL...)"
    ask_model "$session/$turn.prompt.txt" "$session/$turn.json" "$session/$turn.log" "$photo"
    if [ ! -s "$session/$turn.json" ]; then
      # Nothing is recorded, so the conversation stays where it was.
      echo "!! the model call failed: $(grep -m1 '^ERROR: ' "$session/$turn.log" || echo "see $session/$turn.log")"
      continue
    fi
    python3 turn.py render "$history" "$line" "$session/$turn.json"
  done
  echo
}

"$MODE"
