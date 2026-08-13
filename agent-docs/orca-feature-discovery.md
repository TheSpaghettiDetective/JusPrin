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

## 2.1 Project knowledge

This surface answers: **What is this project, and what does someone need to know about it?**

OrcaSlicer exposes model descriptions, creator information, pictures, license information, profile notes, accessories, bills of materials, assembly guides, and other attachments.

The project I inspected contained a safety instruction limiting it to electric tea lights. That information could affect how the agent evaluates material, temperature exposure, and intended use.

### Why the agent cannot decide this alone

The mesh does not contain reliable authorship, licensing, safety, assembly, or use-case information. An agent may extract clues, but it must not treat guesses as project facts.

### What should remain

- Project name and description
- Author and license
- Safety and usage instructions
- Reference pictures
- Assembly instructions
- Bill of materials and accessories
- Important profile notes
- Project save, version, backup, and ownership controls

### What the agent should do

The agent should read this material before planning the print, highlight contradictions, and ask about missing safety or assembly information when relevant.

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

---

## 2.5 Essential geometry editing

This surface answers: **Does the printable geometry need to be intentionally changed?**

This was underrepresented in the previous report. Geometry editing must remain a first-class capability.

### Essential operations

- Add and remove models
- Select parts
- Move, rotate, and scale
- Place a model on a chosen face
- Arrange models on the plate
- Duplicate and change quantity
- Mirror
- Cut and split
- Merge or assemble related parts
- Add text or labels
- Measure dimensions and clearances
- Work with multiple plates
- Repair or identify problematic geometry
- Create intentional modifiers
- Paint supports, seams, materials, or other region-specific behavior

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

### What should be shown

- Selected orientation
- Material assumptions
- Strength strategy
- Support strategy
- Surface-quality strategy
- Expected compromises
- Important deviations from the normal printer profile
- Assumptions the agent could not verify
- Confidence and unresolved risks

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

---

## 2.8 Print-plan verification

This surface answers: **What will the printer actually do?**

OrcaSlicer’s Preview tab is not merely an expert visualization. It is the last opportunity to discover a bad plan before material and machine time are consumed.

### Beginner-facing verification

- Overall appearance of the sliced result
- Estimated time
- Material usage and cost
- Number of layers
- Support locations
- Likely support scars
- Seam locations
- Risky bridges and overhangs
- Thin or missing features
- First-layer contact
- Material changes and purge waste
- Important warnings
- Comparison with the user’s stated intent

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

---

## 2.9 Print preflight

This surface answers: **Is the correct job about to be sent to the correct physical machine?**

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

The AI-piloted application should follow the lifecycle of a print.

## 1. Library

Projects, recent files, printer fleet, ownership, backup, and import.

## 2. Setup

Physical printer, nozzle, plate, filament, AMS mapping, and machine readiness.

## 3. Intent

Purpose, priorities, mechanical requirements, appearance goals, protected surfaces, safety information, and constraints.

## 4. Prepare

The 3D canvas, objects and plates, essential geometry editing, spatial annotation, and agent-proposed changes.

## 5. Plan

A plain-language account of the strategy the agent selected, its assumptions, tradeoffs, and unresolved questions.

## 6. Verify

The sliced result, time and material estimates, visual risks, layer playback, and optional expert toolpath inspection.

## 7. Print

Destination, filament mapping, safety checks, monitoring choices, and explicit authorization.

## 8. Monitor

Camera, progress, alerts, temperatures, pause, resume, stop, and recovery.

## 9. Diagnose

Guided troubleshooting, calibration, maintenance recommendations, firmware, and printer health history.

An **Expert** drawer can expose raw process settings, manual calibration ranges, configuration files, diagnostic tools, and G-code without forcing beginners to understand them.

---

# 4. Visibility hierarchy

## Always visible or one action away

- Current printer, plate, nozzle, and material
- Objects and plates being printed
- Intent and important requirements
- Essential geometry tools
- Agent plan and assumptions
- Preview and major risks
- Print destination and preflight
- Camera, progress, pause, and stop
- Safety and printer-health alerts

## Shown when relevant

- Local surface annotations
- Object-specific requirements
- Material mapping
- Assembly and project instructions
- Calibration recommendations
- Firmware updates
- Storage and timelapses
- Multi-device management
- Import and export choices

## Hidden by default but retained for experts

- Individual speeds and accelerations
- Exact line widths
- Infill and support implementation parameters
- Retraction, wiping, cooling, and purge details
- Raw G-code
- Manual calibration ranges
- Network tests
- Configuration folders
- Developer mode
- Window-layout and visualization preferences

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
