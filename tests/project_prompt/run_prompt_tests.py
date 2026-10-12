#!/usr/bin/env python3
"""Bounded skill-loading and initial-scope smoke tests, not whole-print evaluations.

Return real skill bodies and optionally recorded workspace sections. Stop before
any unrecorded domain result; never invent one. Route timing is reported separately:
an initial read can legitimately precede skill selection.
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

from capture_workspace import replay

HERE = pathlib.Path(__file__).resolve().parent
MODEL = "deepseek/deepseek-v4-flash"
ENDPOINT = "https://openrouter.ai/api/v1/responses"
KEY_ENV = "OPENROUTER_API_KEY"
PROVIDERS = ["Alibaba", "Novita", "Baidu", "AtlasCloud"]
MAX_REQUESTS_PER_TURN = 3
RUN_BUDGET = 24
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
    return rendered


def post(body, key, endpoint):
    request = urllib.request.Request(endpoint, data=json.dumps(body).encode(), method="POST",
                                     headers={"Authorization": "Bearer " + key,
                                              "Content-Type": "application/json"})
    for attempt in range(2):
        try:
            with urllib.request.urlopen(request, timeout=120) as response:
                return json.load(response)
        except urllib.error.HTTPError as error:
            if error.code not in (500, 502, 503) or attempt == 1:
                raise RuntimeError(f"Responses API {error.code}: {error.read().decode(errors='replace')[:500]}")
        except (urllib.error.URLError, ConnectionError):
            if attempt == 1:
                raise
        time.sleep(2 ** attempt)


def invalid_arguments(name, arguments):
    schema = TOOL_SCHEMAS.get(name)
    if schema is None:
        return {"code": "unknown_tool", "message": f'There is no tool named "{name}".'}
    if not isinstance(arguments, dict):
        return {"code": "invalid_arguments", "message": "Arguments must be an object."}
    missing = [field for field in schema.get("required", []) if field not in arguments]
    extra = [field for field in arguments if field not in schema.get("properties", {})]
    if missing or extra:
        return {"code": "invalid_arguments", "message": f"Missing {missing}, unexpected {extra}."}
    return None


READ_TOOLS = {"workspace_inspect", "object_analyze", "slice_report", "slice_inspect",
              "presets_list", "printer_list", "settings_search", "settings_get",
              "view_render", "project_attachment_read", "printer_setup_preview", "object_divide_preview"}


def evaluate(case, opened, domain_tools, reply, with_skills):
    expected = set(case.get("expectedSkills", [])) if with_skills else set()
    # A domain read may precede skill selection. Do not call that a wrong route.
    missing = expected - set(opened)
    routing = "pending" if missing and domain_tools else "missed" if missing else "observed"
    if case.get("noSkillNeeded") and opened:
        routing = "unnecessary"
    read_only = not domain_tools or set(domain_tools) <= READ_TOOLS
    scope_ok = not case.get("readOnly", False) or read_only
    responded = bool(domain_tools or reply.strip())
    return scope_ok and responded, routing


def run_once(case, prompt, key, model, endpoint, providers, with_skills, fixture=None):
    workspace = fixture["workspace"] if fixture else WORKSPACE
    skills = {skill["name"]: skill["text"] for skill in prompt["skills"]}
    instructions = prompt["instructions"] if with_skills else prompt["coreInstructions"]
    tools = TOOLS if with_skills else [tool for tool in TOOLS if tool["name"] != "skill_read"]
    context = case["query"] + "\n\nAuthoritative JusPrin workspace context:\n" + dump(workspace)
    turn = [{"role": "user", "content": [{"type": "input_text", "text": context}]}]
    routing = {"provider": {"order": providers, "allow_fallbacks": False}} if providers else {}
    opened, domain_tools, replies, calls = [], [], [], []
    stopped = False
    stop_reason = "response_limit"
    workspace_reads = 0
    first_input_tokens = None
    total_input_tokens = total_output_tokens = 0

    for _ in range(4 if fixture else MAX_REQUESTS_PER_TURN):
        response = post({"model": model, "store": False, "parallel_tool_calls": False,
                         "instructions": instructions, "tools": tools, "input": turn, "max_output_tokens": 2000, **routing}, key, endpoint)
        usage = response.get("usage", {})
        total_input_tokens += usage.get("input_tokens", 0)
        total_output_tokens += usage.get("output_tokens", 0)
        print(dump({"case": case["name"], "usage": usage}), file=sys.stderr, flush=True)
        if first_input_tokens is None:
            first_input_tokens = response.get("usage", {}).get("input_tokens")
        output = response.get("output", [])
        turn += output
        replies.extend(filter(None, (output_text(item) for item in output if item.get("type") == "message")))
        call = next((item for item in output if item.get("type") == "function_call"), None)
        if call is None:
            stopped = response.get("status") != "incomplete"
            stop_reason = "final_response" if stopped else "incomplete_response"
            break
        name = call.get("name", "")
        try:
            arguments = json.loads(call.get("arguments") or "{}")
        except json.JSONDecodeError:
            arguments = {}
        calls.append({"name": name, "arguments": arguments})
        problem = invalid_arguments(name, arguments)
        if problem:
            result = {"state": "failed", "error": problem}
        elif name == "skill_read":
            skill_name = arguments.get("name", "")
            if skill_name in skills:
                opened.append(skill_name)
                result = {"state": "succeeded", "result": {"name": skill_name, "text": skills[skill_name]},
                          "actionId": "eval-skill", "workspaceRevision": workspace["revision"]}
                if fixture is None:
                    result["workspace"] = workspace
            else:
                result = {"state": "failed", "error": {"code": "unknown_skill",
                          "message": f"Available skills: {sorted(skills)}"}}
        else:
            domain_tools.append(name)
            recorded = replay(fixture, arguments) if fixture and name == "workspace_inspect" else None
            if recorded is None:
                stopped = True
                stop_reason = "unrecorded_read" if name in READ_TOOLS else "mutation_boundary"
                break
            workspace_reads += 1
            result = {"state": "succeeded", "result": recorded,
                      "actionId": f"eval-workspace-{workspace_reads}", "workspaceRevision": workspace["revision"]}
        turn.append({"type": "function_call_output", "call_id": call["call_id"], "output": dump(result)})

    reply = "\n".join(replies)
    passed, routing_status = evaluate(case, opened, domain_tools, reply, with_skills)
    if routing_status == "pending" and stop_reason == "final_response":
        routing_status = "missed"
    return {"stop_reason": stop_reason, "workspace_reads": workspace_reads,
            "passed": passed and stopped, "routing": routing_status, "calls": calls, "opened": opened, "tools": domain_tools,
            "reply": reply, "input_tokens": first_input_tokens,
            "total_input_tokens": total_input_tokens, "total_output_tokens": total_output_tokens}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--case")
    parser.add_argument("--cases", type=pathlib.Path, default=HERE / "cases.json")
    parser.add_argument("--workspace-fixture", type=pathlib.Path,
                        help="Replay captured workspace sections, allowing up to four responses")
    parser.add_argument("--runs", type=int)
    parser.add_argument("--must-pass", type=int)
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--model", default=MODEL)
    parser.add_argument("--endpoint", default=ENDPOINT)
    parser.add_argument("--key-env", default=KEY_ENV)
    parser.add_argument("--provider", action="append")
    parser.add_argument("--prompt-rev")
    parser.add_argument("--without-skills", action="store_true")
    parser.add_argument("--compare-without-skills", action="store_true")
    parser.add_argument("--render-only", action="store_true")
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--output", type=pathlib.Path, help="Save full results for manual review outside the repository")
    args = parser.parse_args()

    prompt = render(args.prompt_rev)
    cases = json.loads(args.cases.read_text(encoding="utf-8"))
    fixture = json.loads(args.workspace_fixture.read_text()) if args.workspace_fixture else None
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
    must_pass = runs // 2 + 1 if args.must_pass is None else args.must_pass
    if not 1 <= must_pass <= runs:
        sys.exit("--must-pass must be between 1 and --runs")
    modes = [False] if args.without_skills else [True]
    if args.compare_without_skills:
        modes = [False, True]

    print(f"model {model} at {args.endpoint}" + (f", hosts {', '.join(providers)}" if providers else "") +
          f"; {runs} runs per case")
    failed = False
    saved_results = []
    total_input_tokens = total_output_tokens = 0
    token_means = {}
    for with_skills in modes:
        label = "with-skills" if with_skills else "without-skills"
        print(f"\n[{label}]")
        mode_tokens = []
        for case in cases:
            with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
                results = list(pool.map(lambda _: run_once(case, prompt, key, model, args.endpoint, providers, with_skills, fixture),
                                        range(runs)))
            saved_results.append({"case": case["name"], "mode": label, "results": results})
            if args.output:
                args.output.write_text(json.dumps(saved_results, indent=2), encoding="utf-8")
            passed = sum(result["passed"] for result in results)
            observed = sum(result["routing"] == "observed" for result in results)
            pending = sum(result["routing"] == "pending" for result in results)
            total_input_tokens += sum(result["total_input_tokens"] for result in results)
            total_output_tokens += sum(result["total_output_tokens"] for result in results)
            tokens = [result["input_tokens"] for result in results if isinstance(result["input_tokens"], int)]
            mode_tokens.extend(tokens)
            ok = passed >= must_pass
            failed = failed or (with_skills and not ok)
            print(f"{'PASS' if ok else 'FAIL'} bounded scope {case['name']}: {passed}/{runs}; "
                  f"routing observed {observed}/{runs}, pending at boundary {pending}/{runs}")
            if with_skills and observed < must_pass:
                print("  Routing needs review: pending reads are inconclusive, not a wrong first action.")
            if args.verbose or not ok:
                for index, result in enumerate(results, 1):
                    if args.verbose or not result["passed"]:
                        print(f"  run {index}: opened={result['opened']} tools={result['tools']} {result['routing']} stop={result['stop_reason']}")
                        if result["reply"]:
                            print("    reply: " + result["reply"].replace("\n", " ")[:500])
        if mode_tokens:
            token_means[label] = sum(mode_tokens) / len(mode_tokens)
            print(f"mean first-request input tokens: {token_means[label]:.1f}")

    if "with-skills" in token_means and "without-skills" in token_means:
        print(f"skill index/tool first-request delta: {token_means['with-skills'] - token_means['without-skills']:.1f} tokens")
    if args.output:
        args.output.write_text(json.dumps(saved_results, indent=2), encoding="utf-8")
    print(f"all-response usage: {total_input_tokens} input + {total_output_tokens} output tokens")
    raise SystemExit(1 if failed else 0)


if __name__ == "__main__":
    main()
