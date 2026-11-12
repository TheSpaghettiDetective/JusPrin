---
name: guide-projects-and-files
description: Read before telling the person how to open, save, find or export a project by hand: Open recent, dropping a file, autosave, Version history, Export project 3MF, and Home.
---

# Projects and files

How JusPrin keeps your work, and how to open, save and export it.

Names in bold are the words on screen. On a Mac the menus named here are in the menu bar. On Windows and Linux the **File** menu is the **File** button in the title bar, and the other menus are in the menu beside it.

These are the by-hand steps. I can open a file the person attached for them; I cannot save, restore or export a project myself, so for those I give these steps.

## Start a project or open a file

Every project is kept by JusPrin itself, in its own folder of projects. A file you open is copied into a new project; the file itself is left as it was.

- **New project** in the **File** menu, or **New** on Home, starts an empty project. The project you were in is saved first.
- **Open…** in the **File** menu, or **Open…** on Home, opens a model or a project file as a new project. The files it opens are STL, 3MF, STEP, OBJ, AMF, SVG and ZIP.
- Dropping a file on Home, or opening one from Finder or Explorer, also opens it as a new project.
- Dropping a model on the 3D view of an open project adds it to that project. A 3MF file opens as a project of its own wherever it is dropped.
- **Open recent** in the **File** menu lists the last ten projects, newest first. It is greyed out while there are none.

The shortcuts are Cmd+N and Cmd+O on a Mac, Ctrl+N and Ctrl+O on Windows and Linux.

## Open a file you opened before

When you open a 3MF file that is already one of your projects, JusPrin asks what to do before it opens anything:

- **Continue current project**, when the file is the project that is already open, keeps working in it.
- **Open existing project** (or **Open most recent project**, when the file made more than one) goes to the project you made from it before.
- **Create new project** opens the file again as a separate project. When the file has changed since you last opened it, this is the button already chosen.

## Saving

JusPrin saves the project by itself: a moment after you stop changing it, and never while AI is in the middle of a change. The project's name in the header of the project is followed by **Saving…**, **Saved** or **Couldn't save**.

- **Save** in the **File** menu (Cmd+S on a Mac, Ctrl+S on Windows and Linux) saves now and keeps that version for good, so it stays in Version history.
- An empty project is never saved. It is saved once it holds a model, and from then on even if you empty it.
- A project without a name takes the name of the first model in it.
- If saving fails, the "…" menu at the right of the header says **Changes couldn't be saved. JusPrin will retry.**, and saving starts again by itself once it can.

## Go back to an earlier version

Each save is kept as a version. JusPrin keeps up to 200 of them, and always keeps the ones you saved with Save and the ones from before and after a change AI made.

1. Choose **Version history…** in the **File** menu, or **Version history** in the "…" menu at the right of the header. The panel at the left opens on the project's versions, newest first.
2. Click **Restore** beside the version you want. Clicking the time does nothing.
3. Click **Restore** in the question that opens.

The model and the project's settings go back to that version. What you had is saved as a version first, so you can go back to it the same way. Chats and print records stay as they are.

To take back one change AI made in a chat, use **Revert to here** beside the change instead; the guide's page on print settings describes it.

## Export the project as a 3MF file

Projects stay in JusPrin's own folder. To give a project to someone, or to open it in another program, export it:

1. Choose **Export project 3MF…** in the "…" menu at the right of the header, or in the File menu choose **Export**, then **Project 3MF…** (Cmd+Shift+S on a Mac, Ctrl+Shift+S on Windows and Linux).
2. Pick where to save it. It is saved first, then written there.

The 3MF file holds the model, the plates and the settings. It does not hold the chats.

## The Project tab

The panel at the left of the 3D view has two tabs, **Plates** and **Project**. It starts hidden: click the button at the far left of the header (**Show the Plates and Project panel**), or choose **Show plates and project panel** in the **View** menu (on Windows and Linux, tick **Plates and project panel**). Then click **Project**.

- The top shows the project's name and whether it is saved.
- The section headed "MODEL" shows the model's title, designer and picture, when the file brought them. Otherwise it says **No title, designer or description yet.** with **Edit Project Info** to add them.
- **Print history**, **Details**, **Version history** and **Export project 3MF…** open those. Details holds the description, the attached files and the designer's profile.

## Your projects on Home

Home lists every project under **Projects**, most recent first, each with a picture of its plate, its name, when it was saved and what happened to it last, such as printed or exported. Click a project to open it.

To get to Home from a project, click the button near the left of the header whose tooltip says **Back to Home**, or choose **Home** in the **View** menu.

- With eight projects or more, **Search projects** and a choice between **Recent** and **Name** appear above the list.
- Dropping a file anywhere on Home opens it as a new project.
- A project cannot be renamed or deleted from Home.
