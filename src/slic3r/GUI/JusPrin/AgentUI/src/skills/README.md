# Project skills experiment — suspended

**Decision: 2026-10-12.** Suspend this feature until real JusPrin tasks demonstrate
a clear need for additional skills. The current `tune-print` and
`troubleshoot-print` files are experimental drafts whose value has not been
established.

This branch preserves the implementation, prompt revisions, and test work as a
checkpoint. The suspension is a development decision, not a runtime feature
flag: the skills still load when this branch is built. This checkpoint is not a
recommendation to merge or ship them.

## Why we arrived at the wrong design

We started by dividing the slicer agent's responsibilities into plausible skill
categories, before identifying what the agent could not do well with its core
instructions and existing tools. That reversed the useful order: observe a real
gap, identify the missing knowledge or procedure, then decide whether a skill is
the right home for it.

The original five skills included preparation, setup selection, slice review,
support strategy, and diagnosis. Some of that content described general agent
responsibilities or operations already explained by tools. The revision moved
preparation obligations into the core prompt and reduced the set to tuning and
troubleshooting, but those two categories still lacked a demonstrated need.
Much of their content repeated general printing knowledge: consider load
direction, walls and infill; distinguish warping from detachment; consider
moisture; ask questions that separate possible causes.

There are potentially useful JusPrin-specific details in the drafts, such as
using semantic regions and interpreting particular slice-report sections. We
did not establish that these details were missing from the agent's available
context, that a separate skill was necessary, or that loading it improved task
outcomes. Plausible advice and a sensible category name are insufficient reasons
to add another layer of instructions.

The earlier design also made skill loading the required first action and tested
exact tool order and question shape. Those constraints encouraged compliance
with an invented procedure. The revision removed them and allowed useful reads
before skill selection. The later smoke tests appropriately checked loading
and scope, but those checks cannot establish whether the skill is useful.

## What the tests established

The loader can advertise metadata and return skill bodies. The captured-workspace
batch showed that authentic workspace inspection can lead to skill loading:
strength tuning loaded its skill in 3/3 runs, and layer-shift diagnosis in 2/3.
Seven other skill-relevant runs reached unrecorded reads before loading a skill,
so their later behavior remains unknown. All 24 runs passed the bounded scope
check.

These results do not show better outcomes than the same agent without skills.
The experiment did not establish reliable completion of preparation, tuning, or
diagnosis. The small smoke-test scope was intentional; its results should not
be used to justify the content or expand testing merely to defend these drafts.
See the [test README](../../../../../../../tests/project_prompt/README.md) for
methods, results, and limitations.

## What would justify resuming

Start with a few real user tasks using the core instructions and existing tools.
Record where the agent needs expert corrections, undocumented application
knowledge, or a repeatable procedure it fails to discover on its own. Determine
whether each gap belongs in a tool, the core instructions, application code, or
an optional skill.

For a gap that does justify a skill, capture the smallest useful procedure and
compare the task outcome with and without it. A small representative comparison
is enough to start. Keep it only if it provides useful improvement; do not
require these two names, two skills, or any skills at all.

Preparation remains a core responsibility: resolve a suitable printer, filament,
and process configuration, preserve valid customizations, distinguish selected
presets from physical facts, verify the resulting slice, and follow Send's
preflight and authorization boundary. Answering a question does not automatically
authorize project changes. Those responsibilities do not depend on this feature.

## Authoring guidance behind this decision

- [Agent Skills best practices](https://agentskills.io/skill-creation/best-practices):
  ground skills in real expertise and add what the agent lacks.
- [Anthropic skill authoring guidance](https://platform.claude.com/docs/en/agents-and-tools/agent-skills/best-practices):
  identify gaps without a skill, establish a baseline, and write minimal
  instructions to address those gaps.
