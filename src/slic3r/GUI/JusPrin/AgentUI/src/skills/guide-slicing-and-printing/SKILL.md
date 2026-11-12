---
name: guide-slicing-and-printing
description: Read before telling the person how to slice, check, print or export G-code by hand, or find past prints: the Print button's menu, Check print, print issues on the 3D view, Print history.
---

# Slicing, checking and printing

How to turn a project into a print: slice it, check it, and send it to the printer or save the file.

Names in bold are the words on screen. On a Mac the menus named here are in the menu bar. On Windows and Linux they are in the menu beside the **File** button in the title bar.

These are the by-hand steps. I can slice and check a plate myself with my tools, and open the window that sends a print; the person always sends the print themselves.

## Slice and print

The button at the right of the header of the project does the next step for the plate you are working on:

- **Slice** while the plate is not sliced. Cmd+R on a Mac, Ctrl+R on Windows and Linux, does the same.
- **Slicing…** while it slices. The button beside it turns into an X, **Cancel slicing**, that stops it.
- **Print** once the plate is sliced, or "Print plate 2" and so on when the project has more plates. It opens the window that sends the print to the printer, where you start it. Switching to a plate that is not sliced brings **Slice** back.

The arrow beside the button (**Print actions**) has the other choices: **Slice all plates**, and once a plate is sliced, **Check Print**, **Print all plates…** and **Export G-code…**. The **Print** menu has the same commands apart from the export, which is under **Export** in the **File** menu.

When the printer has no connection set up, Print says **Printer connection required**: connect the printer first (the guide's page on printers describes it), or export the G-code and print from a memory card.

## Check the print before printing

Choose **Check Print** in the menu beside the Print button, or **Check print…** in the **Print** menu. A window opens with the sliced plate and a report beside it.

- The report lists the print's **Time**, **Filament** and **Cost**, then what may need a look: **Supports**, **Seams**, **Slicer warnings**, **First layer**, **Unsupported islands**, **Crossing paths** and **Paths outside the plate**. When there is nothing to look at it says **Nothing to flag.**
- Click a finding to turn the view to it and show its layer.
- **Legend** shows what each colour of the sliced paths means.
- Close the window with the X at its top right or Escape. It also closes by itself when the plate changes and the slice is out of date.

## Print issues on the 3D view

When something about the plate would print badly or not at all, a button at the bottom right of the 3D view says how many issues there are, for example "2 print issues". Click it to see them. A bubble then points at the object each issue is about.

- **Ask AI** asks AI in the chat to explain the issue and what you can do about it.
- **Ignore this warning** stops counting a warning you have decided to live with. It stays in the list marked **Ignored**; open it there and click **Stop ignoring** to count it again. An issue that keeps the plate from printing cannot be ignored.
- The arrows in the bubble go to the next issue. The X, Escape, or a click on an empty part of the 3D view closes the bubble.

## Save the G-code to a file instead of sending it

Choose **Export G-code…** in the menu beside the Print button, or open the **File** menu and choose **Export**, then **Export G-code…**, and pick where to save it. Take that file to the printer, for example on a memory card.

## Print history

JusPrin keeps a record of every sliced file it sent or saved, with a copy of the file.

- On Home, click **Print history** above the projects, or the history line on a project's or a printer's card.
- In a project, click **Print history** in the Project tab of the panel at the left.

Each entry says where the file went and when, and whether the print finished. **Print again** sends the kept copy to the printer again, **Export…** saves it again, and **Open in chat** goes to the chat the print was made in. Type in **Search prints** to find one.

When you are signed in to a JusPrin account, an entry in Print history offers "Share how it printed and get 25 credits". It opens **Tell us how it printed**: add a photo with **Choose a photo…**, write what happened in at least 200 characters, say whether **It worked**, came out **Partly** or not, and click **Send report**. Each sliced file takes one report.
