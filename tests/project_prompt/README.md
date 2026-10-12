# Project assistant skill smoke tests

**Suspended 2026-10-12.** This suite preserves an experiment whose skill contents
have not demonstrated a benefit. See the [skills decision record](../../src/slic3r/GUI/JusPrin/AgentUI/src/skills/README.md)
for why development stopped and what would justify resuming. The results below
are historical observations, not a recommendation to ship the feature.

This first-version suite checks that the two skills are discoverable and that
initial behavior respects the request's scope. It does not certify a complete
preparation, tuning, or troubleshooting workflow.

The renderer uses the production C++ prompt (including both guidance constants),
the canonical page skill parser and Markdown, and the native-checked in-app tool
catalog. Only names and descriptions go into the initial skill index. Reads
return the actual skill body. Native and page tests cover installation, lookup,
unknown names, metadata-only discovery, and retention of loaded text in history.

## Lightweight live check

Eight requests cover tuning, diagnosis, explanations, a suitability question,
an explicit preset choice, and general preparation. Three runs per request is
a small smoke sample, not a reliable estimate of rare failures. Review results;
do not tune wording until every sample follows an identical path.

The runner stops at the first domain-tool call, final reply, or three responses.
It never feeds canned domain results back to the model. It reports separately:

- **Initial scope:** a reply or a domain call, with no mutation for advice cases.
  This does not assess the quality of an answer or later behavior.
- **Routing:** whether the relevant skill was actually read. A domain read before
  skill selection is **pending**, not a wrong route: later routing is unmeasured.
  Repeated reads or additional useful skills are not exact-sequence failures.
- **Usage:** every response's input/output tokens, plus a total for the batch.

PASS/FAIL and `--must-pass` concern initial scope only (a majority by default).
Routing observations require review; a green scope result with pending routes
is not evidence that skill invocation works. Full replies and call arguments
can be saved with `--output /tmp/project-skills-results.json` for review.
There is no model judge for punctuation, wording, or question count.

Use DeepSeek V4 Flash through OpenRouter with the same pinned providers as the
printer-prompt suite. No OpenAI judge or OpenAI key is needed. Credentials stay
outside the repository in `~/.config/jusprin/prompt-test.env`; never print them.
After the live batch's spend has been authorized:

```sh
(
  . "$HOME/.config/jusprin/prompt-test.env"
  python3 tests/project_prompt/run_prompt_tests.py --runs 3 --verbose \
    --output /tmp/project-skills-results.json
)
```

Use `--case NAME` for a specific scenario and `--runs N` for an explicit budget.
`--without-skills` and `--compare-without-skills` are optional controls, not a
reason to run more batches. The latter doubles the requested runs. If comparing
versions, use one old/new batch close in time and report uncertainty rather than
buying repeated rounds. `--prompt-rev` requires that revision to contain the same
prompt-rendering symbols; otherwise use that revision's own runner.

## Offline checks

```sh
npm --prefix src/slic3r/GUI/JusPrin/AgentUI test -- skills.test.ts App.test.tsx
python3 tests/project_prompt/run_prompt_tests.py --render-only
python3 -m unittest discover -s tests/project_prompt -p 'test_*.py' -v
agent_bridge_tests '[skills]'
```

Regenerate the tool fixture with
`JUSPRIN_UPDATE_PROJECT_TOOLS=1 agent_bridge_tests '[skills]'` after changing a
registry description or schema. The native projection test checks exact parity.

## Lessons carried forward

Source: commit `e7c9a59511`, *docs: record lessons from the project prompt review
in the test README*. The critical lessons for this small revision are:

- Agree the spend before a live run; record every response's usage. Stop on a
  credential or quota limit instead of switching keys.
- Do not lengthen conversations on canned tool replies. A whole-workflow test
  needs the real app or a stateful stand-in that answers the actual calls.
- Read the core prompt, tool descriptions, skills, and app messages together.
  Avoid competing instructions that each demand the first action.
- Keep standing responsibilities in the core prompt, execution contracts in
  tools, and specialized judgment in skills. Loaded skills must preserve scope.
- Pair action requests with advice-only requests. Grade outcomes separately
  from the path; an exact tool sequence or question count is not correctness.
- Small samples are noisy. A surprising result is a reason to inspect the trace,
  not immediately rewrite the prompt or start a large tuning run.

## Preparation acceptance scenarios

These require a real project and are deliberately not claimed by the routing
smoke test. Use a small manual sample before calling preparation reliable:

| Situation | Evidence to inspect |
| --- | --- |
| Suitable existing presets and custom settings | Valid choices retained; no gratuitous stock reset; current slice checked before readiness. |
| Missing printer or process setup | Actual gaps discovered and resolved; consequential questions asked. |
| Incompatible imported presets | Compatibility preview inspected; substitutions and unsaved edits handled explicitly. |
| Selected filament but unknown loaded spool | Configuration not described as physical fact; material uncertainty resolved when consequential. |
| “What is PETG?”, “Would PETG work here?”, “Why did this warp?” | Useful explanation or relevant reads without unsolicited edits or full preparation. |

Slice review is evidence about generated toolpaths, time and material, not proof
of physical strength or machine readiness. Starting a print still belongs to
Send preflight and its authorization boundary.

## First live smoke batch — 2026-10-12

Ran the approved eight cases three times on DeepSeek V4 Flash through the pinned
OpenRouter providers, with the current two skills and no control batch.

- Initial scope: 24/24 passed. These are only the reply or first domain action;
  no domain operation was executed by this runner.
- Troubleshooting: its skill was read in 4/6 relevant runs. The other two read
  workspace state first, leaving subsequent selection unmeasured.
- Tuning: all 6 relevant runs read workspace state first. None loaded the skill
  before the stopping point; this is inconclusive about eventual invocation.
- General PETG explanations and explicit preset requests loaded no skills in
  their 6 runs. Suitability and preparation requests began with workspace reads.
- 28 responses used 283,046 input and 6,278 output tokens (289,324 total).
  OpenRouter reported a summed cost of $0.013074851484.

Manual review found unqualified material/emissions advice in the general PETG
answers, including “no fumes” and “generally safe indoors.” The initial-scope
metric does not evaluate factual quality and must not be presented as doing so.

This batch confirms successful live skill reads and appropriate initial scope,
not that both skills are invoked most of the time or that preparation completes
correctly. The next useful routing check would observe decisions after authentic
workspace results using the app or a stateful stand-in. Do not force skill-first
ordering just to make this bounded test observe more loads. No extra live runs
or prompt changes were made in response to this sample.

## Captured workspace continuation

For routing after inspection, use a small snapshot captured from a real isolated
native app. `capture_workspace.py` performs only reads against the **explicit**
discovery file; it never discovers or controls the user's running app. Start the
shell harness with `--manual-mcp /tmp/a-dedicated-datadir`, prepare the desired
model in that instance, and wait for slicing to finish before capturing:

```sh
python3 tests/project_prompt/capture_workspace.py \
  --discovery /tmp/a-dedicated-datadir/jusprin/mcp.json \
  --model tests/data/jusprin/tee_with_hole.stl \
  --output tests/project_prompt/fixtures/tee-workspace.json
```

The included fixture was captured from the existing native shell harness
(version 02.06.00.51), not a full GUI build of this branch. It contains an actual
50 × 20 × 36 mm tee with a hole, a current native slice, an X1 Carbon 0.4 mm setup,
configured PLA, one object override, and no confirmed physical printer facts.
Its provenance includes capture time and the model SHA-256. The capture checks
that all sections share one session/revision and that merged sections equal a
real combined response. The initial model context is projected using
`OpenAIResponsesAgent::workspace_json`'s field mapping.

```sh
(
  . "$HOME/.config/jusprin/prompt-test.env"
  python3 tests/project_prompt/run_prompt_tests.py \
    --cases tests/project_prompt/workspace_cases.json \
    --workspace-fixture tests/project_prompt/fixtures/tee-workspace.json \
    --runs 3 --verbose --output /tmp/captured-workspace-results.json
)
```

This mode allows four responses per conversation and replays only the requested
workspace sections. Default and empty selection follow the native contract.
Activities are intentionally unsupported: the captured history would not reflect
the new conversation. Other reads and all mutations stop the conversation before
returning any invented outcome. Real skill bodies remain available, and the
runner reports replayed-read counts and the stopping reason.

The MCP tool result is wrapped with state, action ID, and workspace revision.
The richer `AgentHost::context_json` UI context normally attached to in-app tool
continuations is **omitted** because MCP does not expose it. It is not replaced
with fabricated settings or the differently shaped initial context. This replay
therefore measures routing with authentic workspace payloads, not exact native
continuation parity, completion, physical facts, or successful project changes.

A relevant skill not loaded by a final reply is reported as `missed`; that label
is an observation, not proof the response is bad (asking a useful question may
be appropriate). An unrecorded read or the response limit leaves later routing
unmeasured. Inspect the trace instead of penalizing a reasonable read-first path.

### Captured-workspace batch — 2026-10-12

Ran eight cases three times on the same model/provider configuration, using the
current prompt and skills without intervening prompt changes. All 24 completed
within the four-response bound; 17 real workspace reads were replayed.

| Request | Relevant skill loaded | Remaining observation |
| --- | --- | --- |
| Stronger tee | tune-print, 3/3 | All loaded after workspace inspection, then requested further evidence. |
| Less material | tune-print, 0/3 | All requested object analysis next; later routing unmeasured. |
| Why did it warp? | troubleshoot-print, 0/3 | One requested object analysis, two slice reports; later routing unmeasured. |
| Layer shift | troubleshoot-print, 2/3 | One read the workspace then requested a slice report without loading yet. |
| What is PETG? | None, 3/3 | Direct answers; no domain calls. |
| PETG suitability | None, 3/3 | Workspace inspection then object analysis; no edits. |
| Explicit process preset | None, 3/3 | Preset discovery, sometimes after workspace inspection. |
| Prepare existing setup | None, 3/3 | Workspace inspection then slice/custom-setting reads. |

Bounded scope passed 24/24, including no mutation requests in the 12 advice runs.
Five of the 12 skill-relevant runs loaded their expected skill; seven remain
inconclusive at unrecorded reads. There were no final answers without the
expected skill in those 12 runs. This establishes that authentic inspection can
lead to skill loading, but does **not** establish that both skills load most of
the time across requests. No extra batch was run to chase that conclusion.

The preparation cases stopped before resolving loaded filament or reviewing a
slice, so physical readiness and preservation of custom settings remain
unverified. Scope passing does not mean all domain arguments are valid or advice
is factually correct. Detailed schema/operation correctness belongs to native
tool tests or a future real-app continuation, not a prescriptive routing score.

Usage: 46 responses, 479,479 input + 11,849 output = 491,328 tokens. OpenRouter
reported $0.020843405752 total cost. Local full results and usage log are
`/tmp/jusprin-captured-workspace-batch-20261012.json` and the adjacent `.log`.
Eight offline harness tests passed, and the capture's combined-response parity
check passed against the native app. The isolated capture process was stopped.
