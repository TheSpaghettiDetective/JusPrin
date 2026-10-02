# Design-system governance for JusPrin Figma Agent work

Use this guide to decide whether Figma Agent should reuse, compose, extend, or leave a design local in the JusPrin Figma file. The shared library should contain stable product primitives. Feature mockups should remain easy to inspect without crowding the library with delivery artifacts.

The repository is the implementation contract:

- semantic roles and values: `resources/jusprin/ui/design-tokens.json`;
- product use, geometry, typography, accessibility, and handoff rules: `agent-docs/jusprin/design-system.md`;
- surface ownership: `agent-docs/jusprin/architecture.md` and `agent-docs/jusprin/fork-stewardship.md`.

If the current code and Figma disagree, follow the repository until the discrepancy is reviewed as a design decision.

## Classify the thing before building it

### Token or style

A semantic value used across unrelated surfaces: color intent, typography role, spacing scale, radius, elevation, or control sizing.

Reuse the existing semantic value. Add a new one only when the product needs a new semantic role, not because one mockup needs a slightly different number.

### Generic primitive

A small control with a stable interaction contract across features: icon button, tab, menu row, popover, checkbox, badge, disclosure, text role, or swatch.

Reuse the existing primitive. If none exists, promotion can be justified when independent product surfaces need the same behavior and state model.

### Feature composition

A combination whose meaning belongs to one feature: a plate row, project metadata preview, print-history record, account summary, or domain-specific card.

Keep it in the feature as ordinary auto-layout frames/groups or a feature-local component when implementation-oriented prototyping truly benefits from local reuse. Do not put it in the shared design system merely because it repeats across several states of the same feature.

### One-off state

A menu-open example, empty state, comparison alternative, annotation, or review specimen.

Keep it local and ordinary. It does not belong in the shared library.

## Promotion test

Promote a new item to the shared design system only when all relevant answers are yes:

1. **Independent reuse:** at least two separate JusPrin product contexts need it, or the user explicitly requests a shared primitive.
2. **Stable contract:** its purpose, anatomy, interaction, and state model are understood.
3. **Meaningful variants:** variants represent real reusable states, not different example content.
4. **Token fit:** it uses established semantic variables, or a justified new semantic role is being added.
5. **Ownership:** there is a clear library location and name that future designers can discover.
6. **Maintenance value:** promotion reduces future divergence more than it increases library search noise.

If the evidence is incomplete, keep the design local. Promotion can happen later without losing the work.

## Signs of design-system pollution

- Components named after a numbered mockup or a single feature state.
- A component created because an element appears twice in one flow.
- Variants whose only difference is example text, object name, or count.
- Specimen frames created during delivery even though no library documentation was requested.
- Near-duplicate primitives differing by one spacing or color value.
- Feature rows mixed into the same section as generic buttons, inputs, and menus.
- Detached copies of an established primitive used to bypass its contract.
- New raw color, typography, radius, or spacing styles that duplicate semantic variables.
- A visual value added only in Figma with no matching JusPrin semantic role.
- Web-style components whose density or behavior does not fit the native desktop shell.

## Audit sequence

Before creation:

1. Search the local file and enabled libraries for the required primitive.
2. Inspect available properties and variants rather than judging from the component name alone.
3. Identify the nearest approved source frame using the same visual language.
4. Record any actual primitive gap separately from feature needs.
5. Compare token values and role names with `resources/jusprin/ui/design-tokens.json`.

After each Figma Agent step:

1. Inspect the new frame's layers and instances.
2. Check the Assets or design-system page for unexpected definitions.
3. Confirm new feature structures are ordinary frames/groups unless promotion was deliberate.
4. Confirm established components were not modified globally.

At completion:

1. List every design-system addition.
2. State its independent consumers and contract.
3. Remove or demote any addition that fails the promotion test.

## Adding a genuinely missing primitive

When promotion is justified, give Figma Agent a separate bounded task:

- name the library section;
- define the primitive's purpose and anatomy;
- define only real states and properties;
- bind it to existing semantic variables;
- create a minimal specimen or documentation frame only if the library already uses one;
- replace local provisional versions with instances;
- verify existing components and frames remain unchanged.
- record the matching repository token or the approved repository change needed to support it.

Complete and verify this task before using the primitive broadly.
