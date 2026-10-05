---
name: figma-agent-design
description: Create, revise, and review JusPrin product UI in Figma with Figma Agent from wireframes, screenshots, specifications, or open briefs. Use for future JusPrin design work that must follow the repository's product guidance and semantic design system without polluting the shared component library.
---

# JusPrin Figma Agent Design

Use Figma Agent to produce coherent, reviewable JusPrin product design while keeping the Figma file maintainable. Treat the user's request as the source of scope and use attached wireframes, screenshots, documents, and existing frames as design input.

## Ground the work in JusPrin

Before designing a surface that is intended for implementation:

1. Read `agent-docs/jusprin/README.md` and the relevant product documents it points to.
2. Read `agent-docs/jusprin/design-system.md`. Treat `resources/jusprin/ui/design-tokens.json` as the authoritative token source when Figma and the repository disagree.
3. Identify whether the surface belongs to the native wxWidgets shell, the embedded Agent page, or OrcaSlicer-owned UI. Respect the ownership boundaries in `agent-docs/jusprin/architecture.md` and `agent-docs/jusprin/fork-stewardship.md`.
4. Design for a dense native desktop application on Windows, macOS, and Linux using DIP, including narrow-window and high-DPI behavior.

Use the current approved JusPrin Figma file and frames named by the user. Do not infer the approved source from canvas proximity, page order, or a numbered suffix. Older explorations and POC frames are reference material, not automatic sources of truth.

## Establish the starting point

Before prompting Figma Agent:

1. Identify the target Figma file, page, approved source frame, and existing states that must remain unchanged.
2. Inspect the relevant design-system page, variables, styles, and generic primitives. Search for the needed primitives before proposing new ones. Map them to the semantic roles in `resources/jusprin/ui/design-tokens.json`.
3. Determine which starting mode applies:
   - **Wireframe-led:** preserve the wireframe's information architecture, content, behavior, and state distinctions. Add visual fidelity without silently redesigning the product concept.
   - **Existing-design revision:** preserve accepted structure and change only the requested areas.
   - **Exploratory:** when no wireframe exists, define the user problem, primary flow, required states, constraints, and unresolved decisions before creating polished screens.
4. List the frames or states needed and their dependencies. Capture the exact frame names as they are created.

Read [design-system-governance.md](references/design-system-governance.md) when the file has a design system or shared library. Read [prompt-and-review.md](references/prompt-and-review.md) before sending the first substantial Figma Agent prompt.

## Work in meaningful increments

Use one step per design decision, interaction pattern, or coherent state group. Do not reduce the work to one prompt per row, icon, or local frame.

- Establish one approved visual source before creating variants.
- Group variants that share the same settled pattern and can be reviewed together.
- Separate states when they introduce a new navigation pattern, hierarchy, menu, empty state, or data-truthfulness decision.
- Ask Figma Agent to stop after the named deliverable so the result can be inspected.
- Pause for the user when requested or when the next step depends on a real product decision. Otherwise continue after verification.

This balances two failure modes: a large prompt that produces many unchecked mistakes and tiny prompts that create unnecessary components or inconsistent local decisions.

## Protect the design system

Use this order of preference:

1. Existing variables, styles, and semantic tokens.
2. Existing generic primitives with the correct contract.
3. Feature-owned auto-layout frames and groups.
4. A new design-system primitive only when reuse is demonstrated.

Repeated structures inside one JusPrin feature, one screen family, or several states do not by themselves justify a design-system component. Keep feature rows, metadata blocks, local cards, summaries, and state-specific compositions local unless they satisfy the promotion test in the governance reference. Reuse must be demonstrated across independent JusPrin product surfaces, not across examples of the same feature.

When Figma Agent needs a missing item:

- First decide whether it is a generic primitive or feature composition.
- Add a generic primitive to the design system only when it has a stable contract and independent consumers.
- Keep a feature composition local even if it appears in several mockup states.
- Do not create specimen frames, variants, styles, or variables merely to make the current task easier.
- Do not detach or rewrite established library instances unless the requested design cannot be expressed through their intended properties.
- Do not add a Figma-only token or component value that has no corresponding repository role. Raise it as a design decision and, if approved, update the canonical token source as part of implementation work.

If Figma Agent creates accidental components or specimens, detach their feature instances if necessary, remove the new definitions, and verify that established library assets remain intact.

## Prompt Figma Agent precisely

Every creation prompt should name:

- the source frame or design to duplicate;
- the exact new or revised frame names;
- the current step's required state and behavior;
- elements that must remain unchanged;
- whether feature content should remain ordinary frames/groups;
- any existing primitives to reuse;
- content and ordering that must be exact;
- states or concepts that are explicitly out of scope;
- the stop condition and the selection Figma Agent should leave active.

Keep stable constraints short and consistent across prompts. Put most prompt detail into the current state delta. Attach the wireframe or reference when available; when it is not available, give Figma Agent a bounded problem statement and create low-cost structural alternatives before high-fidelity work.

For JusPrin high-fidelity frames, also require:

- paired light and dark states using the same semantic token roles;
- HarmonyOS Sans SC roles and the established functional SVG icon system;
- native desktop density and the documented radius, spacing, control-size, and typography roles;
- applicable hover, pressed, disabled, focus, success, warning, and error states;
- realistic long filenames, localized labels, dense data, empty states, and narrow-window behavior;
- no web-only glass, blur, oversized-control, or platform-specific-font treatment.

## Verify the canvas, not the agent's report

Figma Agent's completion summary is a claim, not verification. Inspect the result visually before moving on.

At minimum:

1. Inspect the whole frame for shell preservation, hierarchy, clipping, overlap, and balance.
2. Inspect the changed region at readable zoom.
3. Check exact text, counts, status labels, menu ordering, selection treatment, disclosure direction, conditional elements, and overlay placement.
4. Check that the frame uses intended variables and existing primitives.
5. Check the file for unintended components, variants, styles, variables, or specimen groups.
6. Confirm no unrelated approved frame changed.

When correction is needed, send a narrow correction prompt that names the affected frames and preserves every accepted detail. Reinspect the corrected result instead of relying on the summary.

After a frame passes, record its exact name and node URL. This prevents later handoff work from depending on memory or canvas search.

## Finish with a consistency pass

Before calling the design complete:

- compare shared structure across all states;
- confirm every required normal, selected, expanded, folded, menu, error, empty, and metadata state that applies;
- confirm conditional content appears only when relevant;
- remove abandoned explorations unless the user wants them retained as decision records;
- confirm the chosen alternative is clearly identified;
- confirm the design system contains only deliberate reusable additions.

For an implementation handoff, reference Figma frame names and node URLs for visual details. Document only behavior, data ownership, interaction rules, conditional logic, implementation boundaries, unresolved questions, and verification requirements that Figma does not communicate reliably.

Tie implementation notes back to JusPrin's ownership model and token source. Call out whether each surface is native, Agent-page, or retained OrcaSlicer UI, because that determines the implementation boundary and the allowed styling mechanism.
