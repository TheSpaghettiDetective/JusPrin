# Figma Agent prompting and review for JusPrin

Use these structures as adaptable prompts. Keep only the clauses relevant to the current task.

## Wireframe-led creation prompt

```text
Create [one coherent state or related state group] from the attached wireframe.

Source:
- Work on page “[page]”.
- Duplicate “[approved source frame]”.
- Create exactly [frame names], then stop.
- Do not modify [approved frames or library areas].

Preserve:
- The wireframe's information architecture, content hierarchy, interaction model, and conditional states.
- The approved shell and existing visual language.
- Existing JusPrin semantic variables and generic primitives.

Design-system boundary:
- Search for and reuse existing [relevant primitives].
- Keep feature-specific content as named ordinary auto-layout frames/groups.
- Do not create components, variants, specimens, styles, or variables unless this prompt explicitly names a justified shared primitive.

Current-state requirements:
- [Exact behavior and content for this step.]
- [Exact ordering, labels, conditional elements, and selection state.]
- Use native desktop density and the JusPrin token, type, radius, spacing, and control-size roles.
- [Explicit exclusions.]

When finished, select [new frame or frames], summarize the change, and stop.
```

## Exploratory prompt without a wireframe

Start with structure, not polish.

```text
Explore the structure for [user problem] in the existing product visual language.

Known constraints:
- [Primary user goal and entry point.]
- [Required information/actions.]
- [Existing navigation or product boundaries.]
- [States that must be represented.]

Use the existing design system and the approved source frame “[frame]”. Keep feature-specific content local and do not add shared components yet.

Respect JusPrin's product boundaries and design for Windows, macOS, and Linux at native desktop density. Use semantic roles from the repository design system rather than introducing raw values.

Create at most [two or three] small structural alternatives for the unresolved decision “[decision]”. Keep everything else the same so the tradeoff is easy to judge. Add a concise rationale outside the product UI and recommend one alternative. Do not proceed to the full high-fidelity state yet.

Select the comparison frame and stop.
```

After the user or evidence selects a direction, create one representative high-fidelity state. Use that approved state as the source for remaining variants.

## Correction prompt

```text
Correction step only for “[affected frame names]”. Do not modify other frames or create new design-system assets.

Fix:
- [Observable defect and exact intended result.]
- [Second defect if tightly related.]

Preserve:
- [Accepted content, layout, menu, viewport, states, and styling.]

Use ordinary editable groups and existing primitives. Select the corrected frame or frames and stop.
```

Avoid asking Figma Agent to “improve” or “polish” a frame without naming the observable problem. Broad correction prompts commonly alter accepted details.

## Visual review checklist

### Whole frame

- Correct source shell and frame name.
- No accidental movement or changes to adjacent approved frames.
- No clipping, overlap, unexpected scroll, or detached overlay.
- Primary hierarchy matches the intended state.

### Changed region

- Exact labels, names, counts, units, punctuation, and ordering.
- Correct selected, active, expanded, folded, disabled, success, warning, and error treatment.
- Disclosures point in the direction implied by current state.
- Menus anchor to the intended target without covering required information.
- Conditional content is absent when its condition is false.
- Empty states do not invent data or expose blank fields.
- Sample or estimated data is labeled honestly.

### Design-system integrity

- Existing variables and generic primitives are used.
- Figma roles match `resources/jusprin/ui/design-tokens.json` and `agent-docs/jusprin/design-system.md`.
- Feature compositions remain local.
- No unexpected components, component sets, variants, styles, variables, or specimens appeared.
- No shared component was globally changed as a shortcut.
- Native density, cross-platform font roles, and DIP-based sizing are preserved.

### Interaction and handoff

- Related selections remain synchronized across views represented in the design.
- Back, tab, disclosure, menu, and action destinations are unambiguous.
- The chosen alternative is marked clearly.
- The frame name and node URL are recorded after approval.

## Progress updates

Report what is being decided or verified, not every UI operation. Useful updates name the state in progress, the specific risk being checked, and any change in direction. Avoid claiming success until the canvas has been inspected.

## Final record

Maintain a short table during the task:

| Frame | Node URL | Source frame | Status | Notes |
| --- | --- | --- | --- | --- |

Use it to produce a handoff that points to Figma for visuals and explains only nonvisual behavior, data semantics, implementation boundaries, and unresolved decisions.

For JusPrin, identify whether each implemented surface belongs to the native shell, the Agent page, or retained OrcaSlicer UI. Link the relevant repository guidance instead of restating visual measurements that are already encoded in semantic tokens.
