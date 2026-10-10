# Project assistant prompt tests

This corpus tests whether the project assistant opens the exact relevant
skills and then takes the right first action: call a domain tool, answer
directly, or ask exactly one question. The requests reproduce the application
boundary rather than testing an isolated router:

- `render_prompt.mjs` bundles the page's own `projectInstructions.ts` and the
  canonical skill files, so the prompt measured here is the text the page
  sends to the app; the script holds no copy of the words.
- `project_tools.json` is the complete in-app tool projection. The native test
  `the project prompt tests send the project assistant's tools as the app does`
  fails when the fixture drifts. Regenerate it with
  `JUSPRIN_UPDATE_PROJECT_TOOLS=1 agent_bridge_tests '[skills]'`.
- The user input includes a fixed workspace serialized in the same shape as
  `OpenAIResponsesAgent::initial_input`.
- Tool calls receive deterministic fake results in the same host envelope as
  the app. Skill reads return the canonical skill body, without frontmatter.

Expected skills are exact. Loading an unrelated skill, missing a required
skill, or reading one twice fails the run. Each case also declares an
`expectedOutcome`; skill selection alone never passes a case.

## Model and repeated runs

Prompt behavior is evaluated on DeepSeek V4 Flash through OpenRouter, pinned
to Alibaba, Novita, Baidu, and AtlasCloud, matching the printer-prompt suite.
One sample is not evidence: the default budget runs every case three times,
and every run must pass. Use `--runs N` and `--must-pass N` to change that.
DeepSeek is always the assistant under evaluation. The same pinned OpenAI judge
as the printer-prompt suite reads only replies whose expectation is “ask
exactly one question,” because punctuation counting cannot distinguish a
rhetorical heading from two facts requested of the person.

On Kenneth's machine, credentials are stored outside the repository in
`~/.config/jusprin/prompt-test.env`. Load them only in the process that runs
the suite; never print or copy their values:

```bash
(
  . "$HOME/.config/jusprin/prompt-test.env"
  tests/project_prompt/run_prompt_tests.py
)
```

Compare the current skills with the same prompt and tools but no skill index or
`skill_read` tool:

```bash
(
  . "$HOME/.config/jusprin/prompt-test.env"
  tests/project_prompt/run_prompt_tests.py --compare-without-skills
)
```

The comparison prints mean first-request input tokens and the measured
skill-index/tool delta. A skill has behavioral evidence only when its case is
more reliable with the skill than without it; merely opening the skill is not
an improvement.

Last verified 2026-10-08 with five runs of all ten cases on DeepSeek V4
Flash: with skills, 50/50 runs passed exact skill selection and the declared
first behavior; without the index and `skill_read`, 40/50 first behaviors
passed. Mean first-request input was 9,745.6 versus 9,239.6 tokens, a 506.0
token cost for the five-entry index and loader definition.

Those figures are for the prompt and skills as first written. Both were
rewritten on 2026-10-10 (`AgentUI/src/projectInstructions.ts` and the five
skills). The rewritten text, one round of five runs per case that day: 46/50
passed skill selection and first behavior, 49/50 chose the expected skill.
The misses were the failed-print case asking two things in one reply (2/5
passed) and one preparation request that skipped its skill. The earlier text
was not rerun alongside; it had passed 50/50 and 48/50 in two rounds that
week.

Budget a run before starting it. Each request carries about 10,000 input
tokens of instructions and tool definitions, and every tool call resends them
with the history, so a run that stops at the first tool costs about 25,000
tokens. Do not lengthen runs on the fixed tool replies in this script: they do
not answer what the model asked, so it wanders, and one such batch used the
month's key limit in October 2026.

Use `--case NAME`, `--runs N`, `--jobs N`, or `--verbose` to investigate a
failure. `--prompt-rev REV` renders a revision that already contains the skill
sources. `--render-only` validates local prompt, tool, skill, and case loading
without making a model call.

## Corpus language

Queries intentionally resemble messy forum and chat requests: terse wording,
slang, typos, assumed visible project state, and symptoms instead of workflow
names. Each is an original composite rather than copied text. The style came
from recurring language in public FixMyPrint, OrcaSlicer, Bambu Lab, Prusa,
and general 3D-printing help threads.

`routingReason` documents the intended distinction for human review and is
never sent to the model.
