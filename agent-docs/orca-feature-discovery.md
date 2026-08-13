# Designing an AI-Piloted OrcaSlicer

## Executive summary

An AI agent can remove much of a slicer’s technical complexity, but it cannot eliminate the need for user interaction.

The boundary is:

> The user tells the system what is physically true, what the object means, and what outcome is wanted. The agent chooses the technical printing strategy. The user verifies consequential decisions and remains in control of the physical machine.

This produces three classes of OrcaSlicer features:

1. **User-decision surfaces must remain prominent.**  
   These capture information the agent cannot reliably obtain: physical setup, functional intent, geometry meaning, aesthetic preferences, legal restrictions, and approval of real-world actions.

2. **Shared agent-and-user surfaces should be redesigned.**  
   The agent proposes a solution, explains it, and draws attention to uncertainty. The user reviews the result visually instead of manipulating dozens of parameters.

3. **Implementation settings can disappear from the primary interface.**  
   The agent can usually manage line widths, speeds, infill mechanics, cooling, retraction, support parameters, and similar technical details.

The final product should therefore retain nine primary surfaces:

1. Project knowledge  
2. Physical setup  
3. Print intent  
4. Objects and plates  
5. Essential geometry editing  
6. Local meaning and overrides  
7. Print-plan verification  
8. Print preflight and live control  
9. Calibration and troubleshooting  

These are not remnants of a legacy slicer. They are the information and control boundary between a person, an AI agent, and a physical manufacturing process.

The interface should not give every retained feature equal weight. Features fall into four visibility levels:

1. **Default** — continuously visible in an ordinary project.
2. **Contextual** — appears when the project, current selection, or agent uncertainty makes it relevant.
3. **On demand** — available through Project details, More, or the expert layer.
4. **Interruptive** — normally hidden, but made prominent when a consequential warning, mismatch, or unresolved decision occurs.

Frequency alone does not determine visibility. A control can be rarely used but interruptive because overlooking it would be costly or unsafe.

---

# 1. Why an AI agent sometimes lacks enough information

Before deciding which UI to retain, it helps to distinguish the different reasons an agent cannot safely make a decision.

## 1.1 The agent cannot observe physical reality reliably

A slicer may say that a particular nozzle, plate, or filament is selected, but that does not prove it is installed.

Examples include:

- The build plate was changed manually.
- The AMS contains a different spool than expected.
- Filament is wet, old, tangled, or nearly empty.
- A hardened nozzle was replaced with a standard nozzle.
- Glue was or was not applied.
- The print bed is occupied.
- A previous print remains attached to the plate.

The agent can detect some of these conditions using printer sensors or a camera, but uncertainty must remain visible.

## 1.2 The agent does not know what the geometry means

A mesh contains shapes, not intentions.

The agent may identify a hole, thin wall, flat face, or overhang, but it does not inherently know whether that feature is:

- A precision bearing surface
- A screw hole
- A snap fit
- A decorative opening
- A sacrificial support
- A face that must remain smooth
- A surface hidden after assembly
- A region intended to flex
- A deliberately thin membrane

This is why essential geometry editing and semantic annotation must remain.

## 1.3 The agent does not know the user’s priorities

There is rarely one objectively correct slicing strategy. A user may prioritize:

- Appearance
- Strength
- Dimensional accuracy
- Speed
- Material savings
- Heat resistance
- Flexibility
- Watertightness
- Minimal support marks
- A particular color arrangement

These priorities can conflict. The agent needs the user to define what “good” means.

## 1.4 Some decisions are subjective

Seam placement, surface texture, layer visibility, support scars, color boundaries, and acceptable print time are personal judgments.

An agent can recommend an option, but it cannot determine the user’s taste from the model alone.

## 1.5 Some actions have consequential real-world effects

Starting a print consumes material, occupies a machine, creates heat and motion, and may run unattended for hours. Firmware updates and calibration changes can alter the behavior of the printer.

The agent can prepare these actions, but the user needs final authority over them.

## 1.6 Some information concerns ownership, law, or safety

The agent cannot invent or silently change:

- Authorship
- Licensing
- Commercial-use permission
- Safety warnings
- Assembly instructions
- Bill-of-materials information
- Intended use
- Privacy and cloud-connectivity preferences

These are authoritative project facts, not slicing parameters.

---

# 2. The surfaces that must remain

## 2.0 Project-window hierarchy

The Project window should not treat Prepare, Preview, and Info as three equal destinations.

- **Prepare is the dominant, persistent workspace.** It is where users inspect the model, communicate intent, manipulate geometry, resolve uncertainty, and review the agent's plan.
- **Preview becomes Check print.** It is a temporary sliced-verification mode, not an equal workspace. Leaving Check returns the user to Prepare.
- **Info becomes Project details.** It is a drawer opened from the project name or menu. The agent reads its contents continuously even when the drawer is closed.
- **Print opens Send preflight.** It never sends directly. Explicit Send remains the consequential commitment.

The normal flow is:

`Prepare -> Check print -> return to Prepare if correction is needed -> Print -> Send preflight -> explicit Send`

### Default Project-window composition

The default window should contain:

- Project name and saved/unsaved state
- Compact printer pointer: target printer, nozzle, build plate, and material
- Compact, editable intent summary
- Compact active plate/object summary
- 3D Prepare canvas
- Essential direct-manipulation tools
- Compact pinned agent plan
- Highest unresolved assumption, compromise, or risk
- Check print
- Print
- Consequential warnings when applicable

The canvas is the primary human-agent collaboration surface. It must make the build plate and model visible, support camera rotation, pan, zoom, selection, and direct spatial manipulation, and show agent-proposed changes before they are accepted.

The compact summaries should keep the governing assumptions visible without expanding full editors. Examples:

> Decorative · appearance first · protect front surface · under 5 hours

> Plate 1 · Fuzzy Heart · 1 copy · PLA Matte

A one-object project should not permanently display a large object tree. A complete intent questionnaire should not remain expanded after it has been answered.

### Direct resolution of uncertainty

Warnings should link to the user action that resolves them rather than expecting the user to translate them into legacy slicer controls:

- `Support enters this hole` -> `Protect this opening`
- `Seam crosses a visible face` -> `Mark visible face`
- `Strength direction is unclear` -> `Show load direction`
- `Part may not fit` -> `Enter critical dimension`
- `Support may be hard to remove` -> `Adjust supports`

Each feature still has one authoritative home, but summaries, warnings, and resolution actions may appear elsewhere. For example, Printer Configuration owns the installed hardware state while Prepare shows a compact pointer and any mismatch.

## 2.1 Project knowledge

This surface answers: **What is this project, and what does someone need to know about it?**

OrcaSlicer exposes model descriptions, creator information, pictures, license information, profile notes, accessories, bills of materials, assembly guides, and other attachments.

The project I inspected contained a safety instruction limiting it to electric tea lights. That information could affect how the agent evaluates material, temperature exposure, and intended use.

### Why the agent cannot decide this alone

The mesh does not contain reliable authorship, licensing, safety, assembly, or use-case information. An agent may extract clues, but it must not treat guesses as project facts.

### What should remain

- Project name and saved/unsaved state
- Project description
- Author and license
- Safety and usage instructions
- Reference pictures
- Assembly instructions
- Bill of materials and accessories
- Important profile notes
- Project save, version, backup, and ownership controls

### What the agent should do

The agent should read this material before planning the print, highlight contradictions, and ask about missing safety or assembly information when relevant.

Only the project name and saved/unsaved state need to remain in the normal Project-window chrome. The rest belongs in the Project details drawer and surfaces automatically only when consequential:

- Safety or usage warning: on import, when it affects planning, and before Print
- License restriction: when sharing, publishing, using commercially, or exporting
- Assembly instructions: for multi-part projects and plate organization
- Bill of materials or accessories: when non-printed components are required
- Reference image: when it clarifies orientation, color, or intended appearance
- Profile note: when the agent proposes deviating from it
- Recovery or version warning: when work is unsaved or versions conflict

The user should not have to open Project details for its contents to influence the plan.

---

## 2.2 Physical printer setup

This surface answers: **What hardware and material will actually perform the print?**

### What should remain

- Target printer
- Nozzle diameter and nozzle type
- Build-plate type
- Loaded filament and material
- Filament color
- AMS or multi-material slot mapping
- Printer availability and current condition
- Important mismatches between configuration and observed hardware

### Why the agent cannot decide this alone

Configured state and physical state can diverge. Selecting “PLA” in software does not establish that PLA is actually loaded, dry, correctly mapped, or compatible with the installed plate and nozzle.

### What the agent should do

- Discover printers and materials automatically
- Read AMS and printer state where available
- Compare observed state with the project
- Recommend compatible profiles
- Explain mismatches
- Ask the user only about unresolved physical facts

The existing setup wizard’s enormous lists of printer and filament profiles should become hardware discovery followed by a short confirmation.

---

## 2.3 Print intent

This surface answers: **What outcome does the user want?**

Instead of beginning with hundreds of process parameters, the product should ask for meaningful requirements.

### What should remain

- Intended use
- Visual versus functional priority
- Strength direction
- Accuracy requirements
- Important dimensions or clearances
- Heat, chemical, outdoor, food-contact, or flexibility requirements
- Desired surface quality
- Maximum acceptable print time
- Material or color requirements
- Acceptable and unacceptable support contact
- Whether this is a prototype or final part

### Why the agent cannot decide this alone

The same model can legitimately be printed with different orientations, materials, wall structures, infill, supports, and tolerances depending on its purpose.

A decorative prototype and a load-bearing final component may use identical geometry but require completely different plans.

### What the agent should do

Translate intent into technical settings. For example:

> “This is a visible decorative object, so I oriented it to protect the front surface, moved the seam to the rear, and accepted a longer print to reduce visible layer artifacts.”

The user should judge that reasoning without having to configure every underlying value.

The full intent editor should appear during project creation, import, or when requirements are incomplete. Afterward, Prepare should retain only a compact editable summary. Specialized questions such as food contact, flexibility, critical clearances, and protected support surfaces should appear contextually rather than in every project.

---

## 2.4 Objects, parts, and plates

This surface answers: **What exactly is included in this job?**

OrcaSlicer’s object and plate tree is essential, although its presentation can be simplified.

### What should remain

- Every plate in the project
- Every object and part
- Object names
- Quantity and duplicates
- Which objects are enabled
- Which material or extruder each object uses
- Modifiers, support enforcers, and support blockers
- Object-specific requirements
- Print order where it matters
- Search for complex projects

### Why the agent cannot decide this alone

The agent cannot assume that every imported object should be printed or that apparently duplicated parts are accidental. A modifier may encode engineering intent that is invisible in the rendered outer surface.

Multi-plate organization can also represent batches, material differences, alternative versions, or assembly groups.

### What the agent should do

Arrange objects, detect accidental overlaps, identify likely duplicates, balance plates, and suggest grouping—but show the resulting job manifest for confirmation.

For a simple one-object, one-material, one-plate project, show a compact job summary by default. Expand the full object-and-part tree contextually when the project contains multiple objects, parts, plates, materials, modifiers, or print-order constraints. Search should appear only when the project is complex enough to need it.

---

## 2.5 Essential geometry editing

This surface answers: **Does the printable geometry need to be intentionally changed?**

This was underrepresented in the previous report. Geometry editing must remain a first-class capability.

### Default operations

- Add and remove models
- Select parts
- Move, rotate, and scale
- Place a model on a chosen face
- Arrange models on the plate
- Duplicate and change quantity
- Undo and redo

### Contextual operations

These remain first-class capabilities but should appear under More, through the selected-object menu, or when the situation calls for them:

- Mirror
- Cut and split
- Merge or assemble related parts
- Add text or labels
- Measure dimensions and clearances
- Work with multiple plates
- Repair or identify problematic geometry
- Create intentional modifiers
- Paint supports, seams, materials, or other region-specific behavior

Examples of contextual surfacing include offering Cut for an oversized model, Measure when fit or clearance matters, Repair when mesh defects are detected, and region annotation when the agent cannot determine where support contact is acceptable.

### Why the agent cannot decide this alone

Changing geometry can change the object’s meaning.

The agent cannot safely assume that it may:

- Cut a model into pieces
- Fill or enlarge a hole
- Change scale
- Move two components relative to one another
- Mirror an asymmetric part
- Emboss text
- Remove an apparently unnecessary detail
- Reorient an assembly
- Create a different mating surface

Even when a change improves printability, it may make the part unusable.

### What the agent should do

The agent may propose geometry changes and generate previews:

> “Splitting the model here eliminates most supports and hides the joint on the rear surface.”

But the user should approve meaning-changing operations. Direct manipulation must remain available because communicating spatial corrections in words is often slower than moving or painting them on the model.

---

## 2.6 Local meaning and region-level overrides

This surface answers: **Are different parts or surfaces subject to different requirements?**

Global instructions such as “make it strong” are often insufficient.

### What should remain

Users need ways to identify:

- Faces that must remain smooth
- Surfaces where support contact is allowed or forbidden
- Precision holes and mating surfaces
- Areas requiring additional strength
- Flexible or sacrificial regions
- Visible versus hidden faces
- Preferred or forbidden seam locations
- Different material or color regions
- Areas where texture is intentional
- Object-specific print requirements

### Why the agent cannot decide this alone

Computer vision can estimate geometry, but it cannot reliably know the functional role of every region.

For example, an agent may place a seam on the geometrically least visible face, unaware that the face mates with another part. It may strengthen the thickest region even though failure occurs around a small mounting hole.

### What the agent should do

Use semantic labels to generate modifiers and technical settings automatically. Users should describe meaning; they should not normally need to configure local speeds, wall counts, or infill percentages themselves.

---

## 2.7 Agent print plan

This surface answers: **What strategy has the agent chosen, and why?**

This is the AI-first replacement for most of OrcaSlicer’s process-setting panels.

### What should be shown by default

- Selected orientation
- Overall material, strength, support, and surface-quality strategy
- Most important assumption the agent could not verify
- Most important compromise or unresolved risk

### What should appear when expanded

- Detailed rationale and alternative plans
- Confidence by decision
- Important deviations from the normal printer profile
- Full change history
- Numerical implementation settings through the expert layer

### Why this surface is needed

Even when the agent has enough information to choose technical values, hiding its entire plan would make errors difficult to recognize.

The user may not need to know that a particular wall speed is 72 mm/s, but they should know that the plan emphasizes surface quality over speed.

### What the agent can normally control

- Layer-height strategy
- Line widths
- Wall and top/bottom structure
- Infill density and pattern
- Speeds and acceleration
- Cooling and temperature within qualified ranges
- Retraction and wiping
- Bridge behavior
- Support style, density, and interface parameters
- Brim, skirt, priming, and adhesion aids
- Purging and flushing calculations
- Seam implementation after the user states the preferred region
- Printer-specific G-code from a trusted profile

These settings should remain accessible to experts, but they do not need to be primary beginner-facing surfaces.

Do not always show time and material estimates in Prepare. Show them there only after a valid background slice exists. Until then, the plan can say `Ready to check`.

---

## 2.8 Check print: print-plan verification

This surface answers: **What will the printer actually do?**

OrcaSlicer’s Preview capability is not merely an expert visualization. It is the last opportunity to discover a bad plan before material and machine time are consumed. In the AI-first product it should become **Check print**, a temporary mode that changes the main canvas into a sliced verification view.

### Default verification

- Overall appearance of the sliced result
- Estimated time
- Material usage and cost
- Highest-priority warnings
- Comparison with the user’s stated intent

When supports exist, support verification becomes the default Check view:

- Support locations
- Contact surfaces and likely scars
- Supports that may be difficult to remove
- Supports entering holes, cavities, or mating regions
- A direct Adjust supports or Mark protected areas action that returns to Prepare

If no supports exist, Check should emphasize the most consequential detected risk instead.

### Contextual verification

- Seams on protected or visible surfaces
- Risky bridges and overhangs
- Thin or missing features
- First-layer contact problems
- Unsupported islands
- Unexpected gaps or collisions
- Material or color changes
- Purge volume and waste
- Prime tower and extruder assignments

### Expert verification

OrcaSlicer’s more detailed views should remain available on demand:

- Line type
- Layer height
- Line width
- Speed
- Fan speed
- Temperature
- Volumetric flow
- Layer time
- Extruder assignment
- Layer-by-layer playback
- Individual toolpath playback
- Visibility toggles for walls, infill, bridges, supports, travel, retraction, wiping, and seams
- Raw G-code inspection

### Why the agent cannot approve its own plan

The preview is produced from the same assumptions and algorithms that created the plan. It is evidence, but it is not independent knowledge of the user’s intent.

An agent may verify that a toolpath is internally consistent while still misunderstanding which surface must remain smooth or which opening must stay unobstructed.

### What the agent should do

Inspect the complete preview automatically and present a short risk-focused report. The user should review the meaningful consequences instead of manually examining every toolpath by default.

Layer count and the complete extrusion-role legend are not beginner decision aids. Keep them in Inspect toolpaths with the other expert diagnostics.

---

## 2.9 Print preflight

This surface answers: **Is the correct job about to be sent to the correct physical machine?**

Prepare should contain a visible Print entry point. Print opens this preflight; it does not bypass it:

`Print -> Send preflight -> explicit Send`

### What should remain

- Job name and thumbnail
- Destination printer
- Selected plate
- Time and material estimate
- Filament and AMS mapping
- Plate-type confirmation
- Bed-leveling choice
- Flow-calibration choice
- Timelapse choice
- First-layer inspection
- Build-plate detection
- Tangle and clumping detection
- AI-monitoring behavior and sensitivity
- Recovery options
- Warnings about disabled checks
- Explicit Send control

### Why the agent cannot decide this alone

This is where digital assumptions become a real physical operation. The chosen printer may be occupied, prepared with another plate, located remotely, or loaded with valuable material.

Some monitoring options also involve privacy, camera use, false-positive tolerance, or printer-specific limitations.

### What the agent should do

Perform the preflight, recommend safe defaults, and block obviously inconsistent jobs. The user should see a concise checklist and authorize sending.

---

## 2.10 Live printing and device control

This surface answers: **What is happening now, and can the user intervene?**

The Device tab revealed that an AI slicer is also partly a printer-control product.

### What should remain immediately accessible

- Camera
- Job progress
- Current layer and remaining time
- Printer state
- Nozzle and bed temperatures
- Important fan or light controls
- Health and fault alerts
- Pause
- Resume
- Stop

Manual movement, homing, detailed fan control, storage, and file management can be contextual or expert functions.

### Why the agent cannot control this invisibly

Sensors and vision systems can be wrong. Users may notice a sound, smell, obstruction, detaching part, or safety concern that the system cannot observe.

Pause and stop are human override mechanisms. They should never be hidden inside a conversation or require the agent’s cooperation.

### What the agent should do

Monitor the job, explain anomalies, recommend intervention, and automatically pause only under rules the user has authorized in advance. It should clearly report why an automatic intervention occurred.

---

## 2.11 Calibration and troubleshooting

This surface answers: **Is the printer-material combination behaving as expected?**

The audit found both guided calibration pages and more advanced generators.

### Relevant calibration capabilities

- Flow dynamics
- Flow rate
- Temperature
- Pressure advance
- Retraction
- Maximum volumetric speed
- VFA
- Junction deviation
- Input-shaping frequency and damping

### Why the agent cannot completely own calibration

Calibration depends on physical evidence:

- Filament may be wet.
- Adhesion may be poor.
- Transparent or reflective filament may confuse optical sensors.
- A nozzle may be worn or partially obstructed.
- A user may judge two samples differently.
- Automatic measurements contain uncertainty.
- An apparently better surface may reduce strength or accuracy elsewhere.

### What the agent should do

The agent should:

1. Diagnose the observed symptom.
2. Decide whether calibration is justified.
3. Select the appropriate test.
4. Choose safe test ranges.
5. Explain what to observe.
6. Analyze sensor data or photographs.
7. Recommend a result.
8. Ask the user to confirm before updating a persistent printer or filament profile.

The user should not be presented with every calibration parameter unless manual control is requested.

---

## 2.12 Privacy, updates, portability, and expert administration

These functions are used less frequently, but they are important for particular users and organizations.

### User-owned decisions

- Cloud login region
- Stealth or offline mode
- Network-plugin use
- Preset synchronization
- Multi-device management
- Firmware and application update timing
- Camera and monitoring privacy
- Backup and recovery policy
- Importing and replacing configurations
- Ownership and export of project data

### Capabilities that should remain available

- Import 3MF, STL, STEP, SVG, OBJ, AMF, ZIP archives, and configurations
- Export models, projects, sliced plates, G-code, and preset bundles
- Export printer, filament, and process profiles
- Manage downloaded and stored printer files
- Access timelapses
- Run network diagnostics
- Open configuration data
- Enable developer tools

### Why the agent cannot decide these alone

These choices affect privacy, compatibility, organizational policy, intellectual property, and the user’s ability to leave the product.

The agent can simplify the workflows, but it should not silently enable cloud communication, install firmware, replace profiles, or trap projects in a proprietary format.

---

# 3. Recommended final product structure

The application should use one main window rather than turning every stage of a print into equal global navigation.

## 3.1 Home

Home is the hub for:

- Printer cards showing availability, condition, and loaded filament
- Project gallery, open, and project status
- Import of 3MF, STL, STEP, SVG, OBJ, AMF, and ZIP files
- Entry to project versions and backups
- Launching Monitor

## 3.2 Project window

The Project window contains the print-planning lifecycle without dividing it into equal destinations:

- **Prepare:** dominant workspace containing intent, objects and plates, the 3D canvas, geometry editing, local meaning, and the agent plan
- **Check print:** temporary sliced-verification mode
- **Project details:** drawer for authoritative project knowledge
- **Print:** entry point to Send preflight

Prepare may point to Printer Configuration, but it does not own machine calibration, filament inventory, maintenance, or privacy settings.

## 3.3 Printer Configuration

Printer Configuration owns:

- Target printer, nozzle, and build-plate configuration
- Loaded filament, material colors, AMS, and multi-material inventory
- Calibration workflows
- Troubleshooting and maintenance
- Firmware-update timing
- Camera and monitoring privacy
- Network plugins and diagnostic tools
- Stored printer files and timelapses

Prepare displays a compact pointer to this state and surfaces mismatches; it does not duplicate the complete configuration UI.

## 3.4 Monitor

Monitor is a pop-out surface so it can remain visible while the user works elsewhere. It owns:

- Camera
- Progress, current layer, and remaining time
- Printer state
- Nozzle and bed temperatures
- Health and fault alerts
- Pause, Resume, and Stop as never-hidden human overrides

Manual movement, homing, detailed fan or light control, storage, and file management are contextual.

## 3.5 Overlays and application-level layers

- **Send preflight:** final job, destination, material mapping, safety, and monitoring confirmation followed by explicit Send
- **Expert layer:** raw process settings, numeric per-object and per-region overrides, manual calibration ranges, toolpath diagnostics, and G-code
- **Preferences:** cloud region, stealth/offline behavior, network plugins, sync, updates, privacy, backup and recovery, import/export administration, and developer tools

This structure preserves the lifecycle of a print without turning Setup, Intent, Plan, Verify, Print, Monitor, and Diagnose into nine equal screens.

---

# 4. Visibility hierarchy

## 4.1 Default

Continuously visible in the ordinary Project window:

- Project name and saved/unsaved state
- Current printer, nozzle, plate, and material summary
- Compact intent summary
- Compact active plate and object summary
- 3D Prepare canvas
- Add, remove, select, move, rotate, scale, place on face, arrange, duplicate, Undo, and Redo
- Compact agent plan
- Highest unresolved assumption or risk
- Check print and Print

Inside Check, the default changes to sliced appearance, time, material, cost, the highest-priority warning, and comparison with intent. When supports exist, support placement and removal risk become the default Check emphasis.

## 4.2 Contextual

Shown when the project or current task makes it relevant:

- Full object, part, and plate tree
- Per-object material or extruder
- Modifiers, support enforcers, blockers, print order, and search
- Local surface annotations and object-specific requirements
- Mirror, cut, split, merge, assemble, text, measure, repair, positive/negative parts, and multi-plate distribution
- Seam, bridge, overhang, thin-feature, first-layer, island, and collision inspection
- Material changes, purge waste, prime tower, and extruder assignments
- Time and material estimate in Prepare after a valid background slice exists
- Assembly instructions, accessories, and reference pictures
- Calibration and maintenance recommendations
- Firmware updates, storage, timelapses, and multi-device management

## 4.3 On demand

Available through Project details, More, or the expert layer:

- Full intent questionnaire after it has been completed
- Project description, author, license, pictures, BOM, assembly guide, and profile notes
- Version, backup, ownership, and export administration
- Detailed agent rationale, alternatives, confidence, profile deviations, and change history
- Individual speeds and accelerations
- Exact line widths
- Infill and support implementation parameters
- Retraction, wiping, cooling, and purge details
- Complete toolpath legend and playback
- Raw G-code
- Manual calibration ranges
- Network tests, configuration folders, developer mode, and visualization preferences

## 4.4 Interruptive

Normally hidden, but presented prominently at the point where the user can resolve it:

- Wrong printer, nozzle, plate, or filament
- Safety or usage instruction relevant to the current plan
- Unsupported, missing, or defective geometry
- Support entering a protected region
- Violated dimensional requirement
- Material incompatible with the stated use
- Agent uncertainty about a meaning-changing geometry operation
- License conflict during sharing, publishing, commercial use, or export
- Unsaved work, recovery issue, or version conflict
- Printer-health or safety alert

---

# 5. The final design rule

For every OrcaSlicer control, ask four questions:

1. **Does this require physical information the software may not possess?**  
   Keep a user confirmation surface.

2. **Does this depend on the object’s purpose, meaning, or the user’s taste?**  
   Keep an intent, annotation, or direct-editing surface.

3. **Does this authorize a consequential, legal, private, or physical action?**  
   Keep explicit user control.

4. **Is this merely a technical method for achieving an already-understood goal?**  
   Let the agent manage it and expose it only when explanation or expert control is needed.

That rule preserves the genuinely human parts of OrcaSlicer while allowing the agent to remove most of its parameter complexity.
