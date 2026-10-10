#!/usr/bin/env python3
"""Run project-assistant skill and first-action evals against a live model.

The request uses the production core prompt, the page's canonical skill files,
the complete in-app tool fixture checked by a native test, and the same
workspace-context shape OpenAIResponsesAgent sends. Each case checks both the
skill reads and the next user-visible behavior: a domain tool, a direct answer,
or exactly one question.
"""

import argparse
import concurrent.futures
import json
import os
import pathlib
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.request

HERE = pathlib.Path(__file__).resolve().parent
MODEL = "deepseek/deepseek-v4-flash"
ENDPOINT = "https://openrouter.ai/api/v1/responses"
KEY_ENV = "OPENROUTER_API_KEY"
PROVIDERS = ["Alibaba", "Novita", "Baidu", "AtlasCloud"]
JUDGE_ENDPOINT = "https://api.openai.com/v1/responses"
JUDGE_MODEL = "gpt-5.5-2026-04-23"
MAX_REQUESTS_PER_TURN = 8
RUN_BUDGET = 30
MAX_RUNS = 20
TOOLS = json.loads((HERE / "project_tools.json").read_text(encoding="utf-8"))
TOOL_SCHEMAS = {tool["name"]: tool["parameters"] for tool in TOOLS}

WORKSPACE = {
    "sessionId": "41",
    "revision": 9,
    "projectName": "Gearbox bracket",
    "printerPreset": "Bambu Lab A1 mini 0.4 nozzle",
    "filamentPreset": "Generic PLA",
    "selectedObjectIds": ["176093659209"],
    "plates": [{
        "id": "176093659140",
        "name": "Plate 1",
        "active": True,
        "sliced": True,
        "objects": [{"id": "176093659209", "name": "bracket", "instances": 1}],
    }],
}


def dump(value):
    return json.dumps(value, separators=(",", ":"), sort_keys=True, ensure_ascii=False)


def output_text(item):
    return "".join(part.get("text", "") for part in item.get("content", [])
                   if part.get("type") == "output_text")


def render(revision=None):
    node = shutil.which("node")
    if node is None:
        sys.exit("node is required to render the project prompt")
    command = [node, str(HERE / "render_prompt.mjs")]
    if revision:
        command.append(revision)
    result = subprocess.run(command, capture_output=True, encoding="utf-8", check=True)
    rendered = json.loads(result.stdout)
    if "# Working on a print" not in rendered["coreInstructions"] or "# Skills" not in rendered["instructions"]:
        sys.exit("rendered project instructions are incomplete")
    return rendered


def post(body, key, endpoint):
    request = urllib.request.Request(endpoint, data=json.dumps(body).encode(), method="POST",
                                     headers={"Authorization": "Bearer " + key,
                                              "Content-Type": "application/json"})
    for attempt in range(5):
        try:
            with urllib.request.urlopen(request, timeout=120) as response:
                return json.load(response)
        except urllib.error.HTTPError as error:
            if error.code not in (429, 500, 502, 503) or attempt == 4:
                raise RuntimeError(f"Responses API {error.code}: {error.read().decode(errors='replace')[:500]}")
        except (urllib.error.URLError, ConnectionError):
            if attempt == 4:
                raise
        time.sleep(2 ** attempt)


def judge_one_question(reply):
    schema = {"type": "object", "additionalProperties": False, "required": ["answer", "why"],
              "properties": {"answer": {"type": "string", "enum": ["yes", "no"]},
                             "why": {"type": "string"}}}
    key = os.environ.get("OPENAI_API_KEY")
    if not key:
        raise RuntimeError("OPENAI_API_KEY is required to judge exactly-one-question cases")
    response = post({"model": JUDGE_MODEL, "store": False,
                     "instructions": "Judge only the assistant reply. A yes means it asks the person exactly one concrete "
                                     "question to answer next. Rhetorical headings do not count, but a checklist or multiple "
                                     "requested facts counts as more than one question.",
                     "input": reply,
                     "text": {"format": {"type": "json_schema", "name": "verdict", "strict": True,
                                         "schema": schema}}}, key, JUDGE_ENDPOINT)
    text = "".join(output_text(item) for item in response.get("output", []) if item.get("type") == "message")
    verdict = json.loads(text)
    return verdict["answer"] == "yes", verdict["why"]


def invalid_arguments(name, arguments):
    schema = TOOL_SCHEMAS.get(name)
    if schema is None:
        return {"code": "unknown_tool", "message": f'There is no tool named "{name}".'}
    missing = [field for field in schema.get("required", []) if field not in arguments]
    extra = [field for field in arguments if field not in schema.get("properties", {})]
    if missing or extra:
        return {"code": "invalid_arguments", "message": f"Missing {missing}, unexpected {extra}."}
    return None


def workspace_result(name, arguments):
    """Small deterministic results in the same host envelope as the app."""
    if name == "workspace_inspect":
        result = {"summary": {"projectName": "Gearbox bracket", "printerPreset": WORKSPACE["printerPreset"],
                              "filamentPreset": WORKSPACE["filamentPreset"], "plateCount": 1,
                              "objectCount": 1, "selectedObjectIds": WORKSPACE["selectedObjectIds"]}}
    elif name == "object_analyze":
        result = {"objectId": WORKSPACE["selectedObjectIds"][0],
                  "mesh": {"sizeMm": [42.0, 28.0, 18.0], "healthy": True},
                  "orientations": [{"name": "current", "overhangAreaMm2": 84.0, "bedContactAreaMm2": 310.0}]}
    elif name == "slice_report":
        result = {"valid": True, "plateId": WORKSPACE["plates"][0]["id"],
                  "summary": {"printTimeSeconds": 5400, "totalGrams": 18.4},
                  "findings": {"items": [], "conflict": "", "toolpathOutsideBed": False, "truncated": False},
                  "islands": {"items": [{"objectId": WORKSPACE["selectedObjectIds"][0], "object": "bracket",
                                           "zMm": 7.2, "areaMm2": 3.1, "supported": False,
                                           "at": [12.0, 9.0, 7.2]}], "truncated": False}}
    elif name == "presets_list":
        result = {"items": [{"name": "Generic PLA", "compatible": True, "selected": True}], "truncated": False}
    elif name == "settings_search":
        result = {"items": [{"key": "layer_height", "label": "Layer height", "writable": True}], "truncated": False}
    elif name == "settings_get":
        result = {"values": {key: {"value": 0.2, "origin": "process preset"}
                             for key in arguments.get("keys", [])}}
    else:
        result = {"accepted": True}
    return {"state": "succeeded", "result": result, "actionId": "eval-action",
            "workspaceRevision": WORKSPACE["revision"], "workspace": WORKSPACE}


def evaluate(case, opened, domain_tools, reply, with_skills, question_verdict=None):
    expected_skills = case["expectedSkills"] if with_skills else []
    skills_ok = sorted(opened) == sorted(expected_skills) and len(opened) == len(set(opened))
    outcome = case["expectedOutcome"]
    kind = outcome["kind"]
    if kind == "tool":
        behavior_ok = bool(domain_tools) and domain_tools[0] in outcome["firstTool"]
        behavior = f"first tool {domain_tools[0] if domain_tools else 'none'}"
    elif kind == "question":
        judged, why = question_verdict or (False, "question was not judged")
        behavior_ok = not domain_tools and judged
        behavior = f"exactly-one-question={judged}, tools={domain_tools}; judge: {why}"
    elif kind == "direct":
        behavior_ok = bool(reply.strip()) and not domain_tools
        behavior = f"direct={bool(reply.strip())}, tools={domain_tools}"
    else:
        raise ValueError(f"unknown outcome kind: {kind}")
    return skills_ok and behavior_ok, skills_ok, behavior_ok, behavior


def run_once(case, prompt, key, model, endpoint, providers, with_skills):
    skills = {skill["name"]: skill["text"] for skill in prompt["skills"]}
    instructions = prompt["instructions"] if with_skills else prompt["coreInstructions"]
    tools = TOOLS if with_skills else [tool for tool in TOOLS if tool["name"] != "skill_read"]
    context = case["query"] + "\n\nAuthoritative JusPrin workspace context:\n" + dump(WORKSPACE)
    turn = [{"role": "user", "content": [{"type": "input_text", "text": context}]}]
    routing = {"provider": {"order": providers, "allow_fallbacks": False}} if providers else {}
    opened, domain_tools, replies = [], [], []
    first_input_tokens = None

    for _ in range(MAX_REQUESTS_PER_TURN):
        response = post({"model": model, "store": False, "parallel_tool_calls": False,
                         "instructions": instructions, "tools": tools, "input": turn, **routing}, key, endpoint)
        if first_input_tokens is None:
            first_input_tokens = response.get("usage", {}).get("input_tokens")
        output = response.get("output", [])
        turn += output
        replies.extend(filter(None, (output_text(item) for item in output if item.get("type") == "message")))
        call = next((item for item in output if item.get("type") == "function_call"), None)
        if call is None:
            break
        name = call.get("name", "")
        try:
            arguments = json.loads(call.get("arguments") or "{}")
        except json.JSONDecodeError:
            arguments = {}
        problem = invalid_arguments(name, arguments)
        if problem:
            result = {"state": "failed", "error": problem}
        elif name == "skill_read":
            skill_name = arguments.get("name", "")
            if skill_name in skills:
                opened.append(skill_name)
                result = {"state": "succeeded", "result": {"name": skill_name, "text": skills[skill_name]},
                          "actionId": "eval-skill", "workspaceRevision": WORKSPACE["revision"],
                          "workspace": WORKSPACE}
            else:
                result = {"state": "failed", "error": {"code": "unknown_skill",
                          "message": f"Available skills: {sorted(skills)}"}}
        else:
            domain_tools.append(name)
            result = workspace_result(name, arguments)
        turn.append({"type": "function_call_output", "call_id": call["call_id"], "output": dump(result)})
        # The corpus judges routing and the first domain action. Its result is
        # still generated in the app's shape, but later workflow steps belong
        # to domain-tool and end-to-end tests rather than this routing metric.
        if name != "skill_read":
            break

    reply = "\n".join(replies)
    question_verdict = judge_one_question(reply) if case["expectedOutcome"]["kind"] == "question" and not domain_tools else None
    passed, skills_ok, behavior_ok, behavior = evaluate(case, opened, domain_tools, reply, with_skills, question_verdict)
    return {"passed": passed, "skills_ok": skills_ok, "behavior_ok": behavior_ok, "behavior": behavior,
            "opened": opened, "tools": domain_tools, "reply": reply, "input_tokens": first_input_tokens}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--case")
    parser.add_argument("--runs", type=int)
    parser.add_argument("--must-pass", type=int)
    parser.add_argument("--jobs", type=int, default=5)
    parser.add_argument("--model", default=MODEL)
    parser.add_argument("--endpoint", default=ENDPOINT)
    parser.add_argument("--key-env", default=KEY_ENV)
    parser.add_argument("--provider", action="append")
    parser.add_argument("--prompt-rev")
    parser.add_argument("--without-skills", action="store_true")
    parser.add_argument("--compare-without-skills", action="store_true")
    parser.add_argument("--render-only", action="store_true")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    prompt = render(args.prompt_rev)
    cases = json.loads((HERE / "cases.json").read_text(encoding="utf-8"))
    if args.case:
        cases = [case for case in cases if case["name"] == args.case]
        if not cases:
            sys.exit(f"unknown case: {args.case}")
    if args.render_only:
        print(f"rendered complete core prompt, {len(prompt['skills'])} skills, {len(TOOLS)} tools and {len(cases)} cases")
        return

    key = os.environ.get(args.key_env)
    if not key:
        sys.exit(f"{args.key_env} is required (or use --render-only)")
    model = args.model
    providers = args.provider or (PROVIDERS if model == MODEL else None)
    runs = args.runs or max(1, min(MAX_RUNS, RUN_BUDGET // len(cases)))
    must_pass = runs if args.must_pass is None else args.must_pass
    if not 1 <= must_pass <= runs:
        sys.exit("--must-pass must be between 1 and --runs")
    modes = [False] if args.without_skills else [True]
    if args.compare_without_skills:
        modes = [False, True]

    print(f"model {model} at {args.endpoint}" + (f", hosts {', '.join(providers)}" if providers else "") +
          f"; {runs} runs per case")
    failed = False
    token_means = {}
    for with_skills in modes:
        label = "with-skills" if with_skills else "without-skills"
        print(f"\n[{label}]")
        mode_tokens = []
        for case in cases:
            with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
                results = list(pool.map(lambda _: run_once(case, prompt, key, model, args.endpoint, providers, with_skills),
                                        range(runs)))
            passed = sum(result["passed"] for result in results)
            behavior_passed = sum(result["behavior_ok"] for result in results)
            skill_passed = sum(result["skills_ok"] for result in results)
            tokens = [result["input_tokens"] for result in results if isinstance(result["input_tokens"], int)]
            mode_tokens.extend(tokens)
            ok = passed >= must_pass
            failed = failed or (with_skills and not ok)
            print(f"{'PASS' if ok else 'FAIL'} {case['name']}: {passed}/{runs}; "
                  f"skills {skill_passed}/{runs}; behavior {behavior_passed}/{runs}")
            if args.verbose or not ok:
                for index, result in enumerate(results, 1):
                    if args.verbose or not result["passed"]:
                        print(f"  run {index}: opened={result['opened']} tools={result['tools']} {result['behavior']}")
                        if result["reply"]:
                            print("    reply: " + result["reply"].replace("\n", " ")[:500])
        if mode_tokens:
            token_means[label] = sum(mode_tokens) / len(mode_tokens)
            print(f"mean first-request input tokens: {token_means[label]:.1f}")

    if "with-skills" in token_means and "without-skills" in token_means:
        print(f"skill index/tool first-request delta: {token_means['with-skills'] - token_means['without-skills']:.1f} tokens")
    raise SystemExit(1 if failed else 0)


if __name__ == "__main__":
    main()
