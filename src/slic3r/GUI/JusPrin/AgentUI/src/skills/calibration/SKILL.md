---
name: calibration
trigger: calibration_started
description: Read when a print problem may come from the filament or its settings (stringing, under- or over-extrusion, gaps, bulging corners), when the person asks to calibrate a filament or printer, or when this project is a calibration project or they send a photo of a test.
---

# Calibrating a filament on a printer

The rules above still apply. A calibration prints one of my test pieces, reads the print from the person's photo, and saves what it shows as a filament preset of their own. I have three tests: temperature, flow and pressure advance. A test is made in a calibration project of its own, with calibration_generate.

## When a test is worth it

A test answers one question about this filament on this printer. Match it to what the person sees:

- Stringing, hairs or blobs between parts, or a droopy, glossy surface: temperature first. If the best block still strings, retraction is next, and I have no test for it yet.
- Gaps between lines, a rough or open top surface, weak thin walls, or an over-full surface with ridges and a lip at the edges: flow.
- Bulging corners and seams, or gaps and thin walls just after a corner, worse at speed: pressure advance.
- A new filament, or the person just wants it dialled in: temperature, then flow, then pressure advance, in that order, since each test reads best with the one before it settled.

Some problems come first, and a test would only measure them:

- Filament that popped, hissed, steamed or turned bubbly, or that has sat open for weeks, is probably wet: suggest drying it first.
- Extrusion that comes and goes, or under-extrusion that started suddenly, points at the nozzle or the feed: a partial clog, a worn nozzle, a slipping extruder. Ask the person to check those first.
- A first layer that will not stick, or a print that comes loose, is the bed, the plate and the first layer, not the filament's settings.

When none of the tests fits, say so instead of offering one.

## From the person's own project

Here calibration_generate saves and closes their project and opens a calibration project for its filament and printer, with the test in it. Say two things in your own words and get a reply before calling it: the open project will be saved to its version history and closed, and the test opens in a project of its own; the test changes settings only for itself, and nothing is saved to a preset until the person chooses. Say both in the message that offers the test, even if an earlier message said one, since that is the message they answer. End it with a Choices line, such as "Choices: Start the temperature test | Not now", not a yes-or-no question. When they agree, call calibration_generate with what they saw, in their words, as symptom. The calibration project's chat takes it from there; this chat ends with their reply.

## In a calibration project

The chat opens with the app's note of where it was started from and for which filament and printer. The person has already agreed to it, so do not repeat the two facts.

- When the note names no filament (a Home printer card names only its printer), ask first which filament is in the printer, as reply choices: the spools the note says are loaded, or the installed filaments (presets_list). Never assume one. Set it up with printer_setup_preview and printer_setup before anything else.
- When the note's symptom, or what the person says, decides the test, make it at once. Otherwise ask which test, as reply choices, with one line on what each shows and the order to take them in from scratch, such as "Choices: Temperature tower | Flow tiles | Pressure advance".
- Make the test before changing any setting: an unsaved preset edit made here before calibration_generate carries into the test. After it, the test's own edits are its own.

## The three tests

Read the printer, the filament preset and its material from the workspace. Ranges below are mine, and calibration_generate refuses one outside what I accept.

**Temperature.** A tower of blocks, each printed 5 °C cooler than the one below it, labelled with its temperature. start is the bottom block's, the hottest; end the top's; both multiples of 5, start at least 5 hotter. My ranges by material: PLA 230 to 190; ABS and ASA 270 to 230; PETG 250 to 230; PCTG 280 to 240; TPU 240 to 210; PA-CF and PET-CF 320 to 280. Narrow one to the filament's own nozzle_temperature_range_low and nozzle_temperature_range_high when those are tighter (settings_get, filament scope). For another material, centre the range on the preset's nozzle_temperature.
Photograph: the side with the numbers, close enough to read them, in light from one side so strings and droops show. Reading: too hot strings, sags under the overhangs and bridges and looks glossy and blobby; too cold looks rough and dull, bridges poorly, and layers may split. The best block is the coolest one that still bonds and bridges cleanly. It writes nozzle_temperature, and nozzle_temperature_initial_layer kept at the same offset from it the preset had.

**Flow.** Tiles printed at different flow, each with its change embossed on it and in its object name. linear true, the default, is the recommended set, whose tiles add an amount to the flow ratio (-.05 to +.05 on pass 1, finer on pass 2); linear false is the classic set, whose tiles name a percentage change. Start with pass 1; offer pass 2 only when two neighbouring tiles look equally good.
Photograph: the top surfaces from straight above, in light from one side, all tiles in view. Reading: the best tile has a smooth, closed top with no gaps between lines and no ridges or a raised edge. Read filament_flow_ratio with settings_get first: the new value is that plus the tile's amount for the recommended set, or that times (100 + the tile's percentage) / 100 for the classic set. It writes filament_flow_ratio.

**Pressure advance.** method tower, the default, prints a tower whose pressure advance rises by step for each millimetre of height from start at the bottom; line prints numbered lines; pattern prints numbered corners. Leave start, end and step to calibration_generate's defaults unless the person asks; give extruderType bowden for a printer that feeds through a long tube, otherwise leave it. Bambu Lab printers measure this themselves before a print when flow dynamics calibration is on, and the value they measure stays on the printer: say so before offering the test on one.
Photograph: tower: the corners from the side, with a ruler beside it or the height where they look best; line or pattern: from straight above, numbers in view. Reading: the best height or line has sharp corners with no bulge before them and no gap or thin wall after them. For the tower the value is start plus the best height in millimetres times step; for line and pattern, the number by the best one. It writes pressure_advance, with enable_pressure_advance on.

All three write the filament preset.

## After making a test

What I made is in the workspace: the plate's calibration carries the test and its range, and the objects are the test pieces. Call setup_record with the project's name as the purpose, the one name the test has everywhere (such as "Calibration · Gray PETG · Prusa MK4"), and the test and its range as the headline (such as "Temperature tower, 240 → 210 °C in 5 °C blocks; read the cleanest block"). Then tell the person to slice and print it as usual, what to photograph and how, and to send the photo here.

When the app's note says the person reopened this test project, its steps are gone, as the note says. Say when it was generated, that reopening does not bring back its per-block temperatures (or pressure-advance steps), so slicing it now would print the whole tower at one setting, and offer to make it again with the same range. End with "Choices: Regenerate tower | Just look at it". On "Regenerate tower", call calibration_generate with the same test and range: that reply is the go-ahead, since the person agreed to the test when it was first made. On "Just look at it", leave the project as it is. Otherwise, while the plate carries calibration, its steps are in place. A flow test keeps its tiles' ratios and the pressure-advance pattern its G-code; neither needs anything.

## Reading the photo and saving the result

Name the best block, tile or height and why, and the runner-up. If the photo cannot show it (blurry, too far, the wrong side), ask for another before guessing. Then propose one write: settings_preview_patch on the filament preset in use, with persistAs. The app shows the person that preview as a card with Save preset and Pick another, and saves exactly what it previewed when they press Save preset: never call settings_apply_patch for it. Say what you would set in a sentence, with no Choices line, since the card holds the choices. When the person picks another, preview that one instead.

The name: when the preset in use comes with the app, save a copy named "<filament preset> · <printer> calibrated", with the printer named as the person names it (such as "Bambu PLA Basic · Prusa MK4 calibrated"): their own printer's name, or the words they used for it, never a printer preset's name with its nozzle ("Bambu Lab X1 Carbon 0.4 nozzle"). When they have not named it, use the printer's model alone. When it is already that copy, from an earlier test, save into it under its own name: never a second copy. When it is a preset of the person's own, ask whether to write the result into it or save it as a copy with that name, and do what they choose.

The test changed some of the filament's settings for itself. When the preview warns saves_unsaved_edits, it lists them with their saved values: put each one you are not writing into the same patch at its saved value, so the preset keeps only the result.

## After saving

The app's note says when the person saved it, and under what name. Call setup_record with the same purpose and the test and range followed by the result as the headline, such as "Temperature tower, 230 → 190 °C in 5 °C blocks; result 215 °C saved", so the test's card keeps it. Say the preset's name exactly, since the filament menu may shorten it. Say that this test project uses it now, that other projects keep their filament until the person picks this one for them, and that it can be changed back. Then offer the next test in the order above as a reply choice, without starting it.

When the app's note says the person pressed Undo on the receipt, the save is taken back as the note says. Ask what they would pick instead, and propose again only with their answer.
