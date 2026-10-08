# Project assistant prompt tests

This corpus tests whether the project assistant opens the exact relevant
skills and then takes the right first action: call a domain tool, answer
directly, or ask exactly one question. The requests reproduce the application
boundary rather than testing an isolated router:

- `render_prompt.mjs` reads the production C++ core prompt, including the full
  `kPrintJourneyGuidance` initializer, and the canonical page skill files.
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
