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

## Lessons learned

Written after a week of reviewing and rewriting the project assistant's prompt
and skills in October 2026. They apply to any change to a prompt, a skill or a
tool description in this repository, and to any test that calls a live model.

### Measuring

- **Agree the spend before a live run.** Every request carries roughly 10,000
  input tokens of instructions and tool definitions, and each tool call resends
  them with the history. State the number of runs and a token estimate, get the
  batch approved, log `usage` from every response, and report the actual total.
  A provider error about a key limit means stop; do not switch keys.
- **Never lengthen a conversation on canned tool replies.** A reply that does
  not answer what the model asked makes it wander until the request cap. About
  1,300 such runs cost roughly 200 million tokens and used a key's monthly
  limit. They also measured the wrong thing: most of the unrequested changes
  they counted came after a dozen empty reads, so the test provoked the
  behaviour it was counting. For whole conversations, use a stateful stand-in
  whose replies answer the call.
- **Small samples flip.** At rates of a few per cent, five or ten runs say
  nothing: one conclusion reversed between 10 runs and 50. Model hosts also
  vary by the hour, so run the old and the new text side by side in the same
  batch. One control, one round. When a result is unclear, report the
  uncertainty instead of buying another round.
- **Grade what a run produced, and say which kind of number you are quoting.**
  What changed, the final state and the answer are outcomes. Which skill was
  opened and which tool came first are the path. Path checks reward the wording
  they were tuned on: a rewrite scored lower on this corpus while doing no
  worse on outcomes.
- **Test both directions.** Pair each request that should act with one that
  should only answer, so a fix for one cannot quietly break the other.
- **A number in a comment or a document needs a test behind it.** Name the test
  it came from, and take the number out when that test turns out to be flawed.
- **Check a finding on a second run before stating it,** and correct an earlier
  statement plainly when a later run contradicts it.

### Writing prompts, skills and tool descriptions

- **Read everything the model receives before changing one part:** the prompt,
  every tool description, the skill list, the skill bodies and the app's own
  messages. Conflicts live between parts, not inside one.
- **One home per rule.** Standing rules go in the prompt. A tool description
  says how that tool works. A skill holds judgement for one situation, and its
  description says what it covers and when to use it. A rule stated twice
  drifts, and the model is left to reconcile the copies.
- **Look for two instructions that each claim the first move.** A "look at the
  project first" step and a "read the skill first" rule in one prompt cost the
  skill read in 5 runs of 50.
- **The most specific, most recent text wins.** A skill loaded in the middle of
  a conversation can outweigh a general rule far above it. Write skill steps as
  judgement, and mark any step that changes the project as applying only when
  the change was asked for. Test a prompt rule with the skills loaded, not only
  without them.
- **Stating a rule once is weaker than stating it three times.** When removing
  repetition, measure the case the repetition was guarding: one such case went
  from 5 of 5 to 0 of 5.
- **Be explicit, give the reason, and give a way out.** Name the action ("do
  not start a new slice"), say why, and say what to do when the evidence is
  missing; otherwise the model acts to obtain it. Use plain wording, without
  capitals for emphasis.
- **Keep the person's own words in skill descriptions.** The description is
  what the model matches a request against.
- **Tool definitions dominate the context.** Here they are about ten times the
  size of the prompt, so a sentence saved in the prompt matters less than a
  tool that need not be loaded.

### Working alongside other sessions

- **Check the base before building on a branch.** A shared branch may have been
  amended since you branched from it; compare the merge base with its tip.
- **Do not work in another session's checkout,** and treat its uncommitted
  files as someone else's.

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
