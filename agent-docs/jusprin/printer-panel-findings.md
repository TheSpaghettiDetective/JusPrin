# Printer conversation panel — findings

What is wrong, and the evidence for it. No recommendations: how to fix each one is the implementer's call.

Build: worktree `printer-conversation-panel-af11cb`, commit 0e55c4f9be ("Talk to the agent about a printer, where the printers column is"), macOS, RelWithDebInfo.
Method: the built binary was run against throwaway copies of the JusPrin data directory with an isolated `$HOME`, driven by real clicks and typing, with the live OpenAI-backed in-app agent. Three runs: a walk through both flows, a run with the agent's API key removed, and a run typing what a person who has never seen the flow would type. Paths are relative to that worktree.

Severity: **Critical** — the user is blocked, or ends up with the wrong printer without knowing. **High** — the flow completes but misleads, loses work, or an agreed part of the design is absent. **Medium** — confusing but recoverable. **Low** — polish.

Not covered: network "Use this" (nothing on this network), dragging a photo onto the panel, dark theme, Windows.

## What works

The printers column is replaced by the panel and "‹ Printers" returns to the list. Recognition from an exact model name works, and from a photo of a printer. "Add this printer" saves the printer. The change conversation opens from a printer card, the nozzle change goes through an approval card, the pinned card shows "0.6 mm · changed", and the change is real and persists: the saved profile now inherits `Bambu Lab A1 mini 0.6 nozzle`. Replies stream, so there is feedback while the agent is writing.

## Critical

**1. Ordinary phrasing produces a false "not found" for printers that are in the catalog.**
"i built it myself, a voron 2.4 350mm" → "I couldn't find a packaged profile that matches a Voron 2.4 350mm." "voron 2.4 350" → found immediately. The catalog contains `Voron 2.4 350`. Same failure for "the ender with the touchscreen", "not sure, either the v2 or the s1", and "maybe ender 3 v2 or ender 3 s1", while "ender 3 v2" matched.
Mechanism: `OrcaPrinterBackend::search_catalog` (src/slic3r/GUI/JusPrin/PrinterSetup/OrcaPrinterBackend.cpp:103) keeps a model only when **every** word of the query is a substring of "vendor + model + build volume + model id"; the session instructions (src/slic3r/GUI/JusPrin/PrinterSetup/PrinterConversation.cpp:191) tell the agent to search with "the person's own words". "350mm" is not a substring of "350 × 350 × 325 mm".

**2. A common nickname matched another manufacturer's printer, and the agent stated it as certain.**
"x1c" → card "Orca Arena Printer Orca Arena X1 Carbon" with "I found one clear match and assumed a 0.4 mm nozzle with PLA." Both `Bambu Lab X1 Carbon` (BBL) and `Orca Arena X1 Carbon` (OrcaArena) are in the catalog; the Bambu entry's text does not contain the literal "x1c". Correcting it with "no it's the bambu one" worked, but only because the tester knew.

**3. Truncated search results are presented as the complete set.**
"prusa" → three cards (CORE One, CORE One HF, CORE One L) and "I found three Prusa matches that fit 'prusa'". The catalog ships 13 Prusa models; MK4, MK3S and MINI were not among them. "prusa mk3s" then matched MK3S. The search returns the first N in catalog order and the result carries no total and no truncation flag.

**4. With no agent set up the panel is a dead end.**
It opens the normal add conversation — greeting, photo tip — with the text box disabled and reading "The Agent is not available", and the Photo button inert. No setup card, no link, no explanation. The agreed behaviour was the embedded agent-setup page the old dialog used. Verified with a data directory whose `jusprin_agent` entry was removed.

**5. A printer that genuinely is not in the catalog has no exit.**
"i built it myself, a voron 2.4 350mm" → "tell me the exact controller or kit name"; "it runs klipper on a btt octopus board" → "give me the exact Voron variant name". The manual wizard is never offered, the shipped `Generic Klipper Printer` profile is never mentioned, and the header's "Set it up myself" link is never referred to.

**6. There is no way to browse what the app supports.**
"actually can you show me a list of the brands you support so i can pick" → "I can't show a brand list from here." The brand and model grid exists only behind the header link.

## High

**7. The agent offers help it cannot deliver, and leaves the product.**
"how do i connect it to my computer?" → USB and PrusaLink/PrusaConnect advice plus "I can help you with the simplest setup". "yes please, set it up so i can print to it" → "I can't set it up directly from here… If you tell me Windows, Mac, or Linux, I'll give you the exact next steps", while running on the user's Mac. There is no connection tool in the session and no Connect… entry point in this build.

**8. "Printer settings…" in the Prepare header opens the ADD conversation.**
From a project with "Generic Klipper Printer" selected, the menu item navigated to Home and showed "NEW PRINTER / What printer do you have?" instead of a conversation about that printer. That printer is a shipped profile rather than a saved printer.

**9. Nothing states what the flow does or where it ends.**
The opener asks for a printer name. It never says that this adds a printer profile, that nothing is being connected, that nozzle, plate and filament will be assumed and corrected later, or what happens after "Add this printer".

**10. Leaving the conversation destroys it silently.**
"‹ Printers" mid-conversation discards the thread with no warning; reopening starts a new session. Cancelling the wizard reached through "Set it up myself" closes the panel and loses the thread the same way.

**11. Every printer's name is doubled, and the doubled name is saved.**
"Prusa Prusa MK3S", "Bambulab Bambu Lab X1 Carbon", "Creality Creality Ender-3 V2", "Orca Arena Printer Orca Arena X1 Carbon". The saved printer appears truncated in the Home list ("Bambulab Bambu Lab …").

**12. Rejected and superseded printers stay live.**
After "Not this one", the pinned card still holds the rejected printer and "Add this printer" is still enabled. After a correction, the wrong card remains in the thread above the right one, looking equally current.

**13. A sent photo stays staged in the composer.**
After sending, the thumbnail and "add a note, or just send" remain, so the next message would carry the same photo again. Reproduced twice.

**14. An assistant message was rendered twice, concatenated.**
"Okay—tell me the brand, model, or what the printer looks like, and I'll match it.Okay—tell me the brand, model, or what the printer looks like, and I'll match it."

**15. The approval card does not say what will change.**
It reads "Change this printer / printer_change · jusprin-native / Waiting for your approval". It never says 0.4 mm → 0.6 mm, and it shows the internal tool name to the user.

**16. After adding: no confirmation, no next step, and the new printer is appended at the bottom of the list.**
The card's status dot is unexplained, so it is not apparent whether the printer is connected.

**17. Adding a printer marks the open project as modified.**
The window title changed from "Untitled" to "*Untitled" immediately after "Add this printer", so a save prompt appears later for a project the user never edited. Observed once; not isolated further.

**18. The agent tells the user to tap things that are not tappable.**
"Tap the card to add it", "Tap 'Add this printer' on the card". The cards are inert; the button is docked at the bottom of the panel, separated from the card by empty space, and when several cards have accumulated nothing indicates which one it applies to (it is the newest).

## Medium

**19. The pinned card is unexplained.**
"NEW PRINTER / Printer — / Nozzle — / Plate — / Filament —" is shown before anything is typed. In the add flow the "· assumed" suffixes were missing, values were preset jargon ("Textured PEI Plate", "Bambu PLA Matte @BBL A1M"), and Plate stayed blank for the Prusa.

**20. The composer placeholder is clipped and demonstrates phrasing that fails.**
It renders as `e.g. "bambu a1 mini" or "not sure, the small` with the rest cut off, and "not sure, the small one" is the kind of phrasing finding 1 breaks on.

**21. Search quality is invisible.** No result count, no "showing 3 of 13", no way to ask for more, so an absence cannot be told from a truncation.

**22. Empty agent bubbles.** One to three avatar dots with no text appear before each reply while tools run.

**23. "Set it up myself" is unexplained and disruptive.** No hint that it opens the standard printer wizard; the wizard opens as a floating dialog over Home with the panel still behind it, and nothing the agent inferred is pre-selected.

**24. Photos.** A staged photo sits above the composer box as a wide file row rather than a thumbnail inside it, and a sent photo appears in the thread as a file name rather than the picture.

## Low

**25. No follow-up suggestions after a change.** The nozzle change produced no chips; the wireframe offers "Use 0.3 mm layers" / "Keep 0.2 mm".
