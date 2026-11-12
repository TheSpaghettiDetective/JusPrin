---
name: guide-print-settings
description: Read before saying where in the app the person can see or change print settings by hand: the setup summary, All settings, the printer, filament and plate settings windows, one object's own settings, taking a settings change back, and classic view.
---

# Seeing and changing print settings

Where the settings of a print are shown in JusPrin, and the ways to change them.

Names in bold are the words on screen. On a Mac the menus named here are in the menu bar. On Windows and Linux they are inside the menu at the left of the title bar.

This guide is the by-hand route. When the person wants a setting changed and has not said they want to do it themselves, I change it with my settings tools. I give these steps when they ask where something is or want to do it themselves.

## See the settings a print will use

The **Setup summary** card at the top of the AI panel names the process and filament presets in use and says how many settings differ from them. If the AI panel is hidden, open the **View** menu and choose **Show AI panel** (on Windows and Linux, tick **AI panel**).

To see every setting:

1. On the Setup summary card, click **All settings**. The link is there once AI is set up.
2. Under **Showing**, pick **Whole project**, one plate, or one object. Plates are listed when the project has two or more. The list also follows what is selected in the 3D view.
3. Type in **Search settings** to find a setting by name, or switch from **All** to **Changed** to see only the settings that differ from the presets.
4. Click **Back to chat** to close the list.

The settings are grouped as Quality, Strength, Speed, Support, Multimaterial and Others, then Filament and Printer. The list is for reading: a value cannot be typed into it.

## Change a print setting

Print settings are things like layer height, infill, supports and speed. JusPrin's own view has no field to type them into. There are two ways to change one:

- Ask AI in the chat, for example "set the layer height to 0.16 mm". The change is then listed in the chat with the old and the new value.
- Switch to classic view, which has every print setting on screen. See Classic view.

## Take a settings change back

- Each change made in a chat is listed in that chat. Point at a change and click the curved-arrow button at its right, named **Revert to here**. Then click **Revert to here** in the question that opens. The model and the project's settings go back to that point. The current model is saved in Version history first, and the chat, exported files and finished prints stay as they are.

- When a preset was saved from the chat, a line reading "Saved:" with the preset's name follows. Click **Undo** on that line to bring its earlier values back.
- To go back to an earlier state of the whole project, open the **File** menu and choose **Version history…**.

## Change printer or filament settings

These open a window with every setting of the printer or of a filament.

- Printer: click the printer's name in the header of the project, then **Printer settings…**. The **Printer** menu has the same item. With no printer chosen yet, it starts adding a printer.
- Filament: click the filament in the header of the project, then **Filament settings…**. With several filaments, pick one from the list first. The **Printer** menu has the same item for the first filament.

## Settings for one plate

1. In the **Plates** tab at the left, open the plate's "⋯" menu, or right-click the plate.
2. Choose **Plate settings…**.

The window holds what can differ per plate: the plate's name, its bed type, the print sequence, spiral vase, and the filament sequence of the first and the other layers. A setting made there shows in All settings with the note "Set on this plate".

## Settings for one object

An object can have settings of its own that differ from the project's. Its row in the **Plates** tab then carries a diamond mark, and the mark's menu says how many it has.

- To see them, open **All settings** and pick the object under **Showing**.
- To take one off, click **Use project value** on its row. A support blocker or enforcer has **Remove**.
- To add one or edit its value, ask AI in the chat, or use classic view.

## Classic view: every setting on screen

Classic view is the app's original screen, with all print settings in a panel at the side.

1. Open the **View** menu and choose **Switch to classic view…**. The "…" button in the header of the project has the same item.
2. A bar across the top says the window is in classic view. Click **Back to JusPrin** on it to return. The **View** menu also has **Switch to JusPrin view**.
