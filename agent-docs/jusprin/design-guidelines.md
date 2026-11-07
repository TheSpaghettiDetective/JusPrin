# JusPrin design guidelines

**Status:** Canonical guidance for `jusprin-newui` on how a screen gets designed and checked, and on the words it shows. The visual system (tokens, type, color, components, logo) lives in [Design system](design-system.md); this document does not repeat it.

Design agents tend to write on-screen text that only they can read. The agent knows how the screen works, so a line like "The sixth digit signs you in." reads clearly to it and to any reviewer who knows the same. A person opening the app for the first time has to decode it, and often can't. The First-Launch Confusion Review (https://claude.ai/artifact/91wmdS3QU8UkzbPi1zog15, 2026-11-06) found 23 places on the first launch where a newcomer would misread or stall, and most of them were wording. The rules below exist to stop that at the source and to catch what still gets through.

## How a screen gets designed and checked

1. **Start from the person.** Before drawing, write down in two or three sentences who reaches this screen, what they know at that moment, what they came to do, and what they must decide here. Every line on the screen serves one of those.
2. **Draw the whole screen, not a component alone.** A board for review is the full screen at the app's size, with the header, panes and neighbouring content the person really sees there, on the JusPrin v2 canvas (https://claude.ai/artifact/ENCtdaz7gjynfbcy4JYbWJ). Build it from the Design System artifact's coded components and the app's own markup, never by copying an older board. Repeated compositions are imported by name, as [Design handoff requirements](design-system.md#design-handoff-requirements) rule 9 says.
3. **Fill it with real strings.** Text that summarises data comes from the rule that will produce it in the app, run on real data. If only a model could write a line, say so on the board.
4. **Write every line by the wording rule below.**
5. **Run the cold-reader check on the boards** (below). Rewrite and check again until every line and button reads as intended.
6. **Show the board for review by what a person sees.** Name the screen as the person would ("the welcome screen, after typing an email"), give the canvas page it is on, and attach the picture. Board ids and builder labels mean nothing to a reviewer.
7. **Build it** following [Writing UI code](design-system.md#writing-ui-code).
8. **Check it in the running app.** Take screenshots of the built screen along one real path and run the cold-reader check again. The guards prove the code went through the tokens; only the screenshots show what the person reads.

## The cold-reader check

The writer of a screen cannot see that its text is cryptic, so the check is done by a reader who knows nothing about how the screen works.

- Start a fresh agent. Give it only the screenshots and one or two sentences a real user would know (for example: "You own a 3D printer and just installed JusPrin, a desktop app that prepares 3D models for printing."). Give it no code, spec, design notes, chat history or list of suspected problems, and rename files so their names don't hint at anything.
- Use screenshots from one coherent path, in the order a person meets them. Mixing signed-in and signed-out screens, or states set by hand, makes the reader report contradictions that no user would see.
- Ask it, for each screen, to quote every line and every button exactly, say in one plain sentence what the line means or what the button will do, and list where it would hesitate, misread, worry, or not know what to do next, including information the screen leaves out. Tell it to read literally, like a busy newcomer: a line it had to puzzle out counts as a problem even if it worked it out.
- Compare its answers with the intent written in step 1. Any line it reads differently from the intent gets rewritten, however small the gap. Its other hesitations are input to weigh, not orders.

What the check is known to catch: on 2026-11-06 a fresh agent given only the review's ten screenshots fully caught 14 of the 23 problems and partly caught 4 more. It caught every cryptic phrasing in the list. It missed problems that need knowledge from outside the screen (that Skip installs a 250 mm Klipper printer) and finer editorial calls (a redundant reassurance, one allowance under three names, "Connected" used for an account). It also raised real questions the review had not, such as what happens after the free month. It has not been compared with a real first-time user, so for the screens that matter most, such as first launch, a person remains the final test.

## Wording

Every line says what the person does or what happens to them, in a full sentence, in words a newcomer already has.

The five patterns below are what the review found most. Each pair is the text the app showed and the review's proposed rewrite; the rewrites are proposals, not yet in the app.

1. **Facts joined by a middle dot.** The dot drops how the facts relate, and the reader guesses.
   - Before: "Signed in as new@example.com · 500 credits" (reads like a balance the person already had)
   - After: "You got 500 free credits for signing up.", with the email address on a smaller line under it
2. **Metaphors.** The reader has to work out what the image stands for.
   - Before: "New or existing account, same door."
   - After: a "Sign up free" button, with "Already have an account? Sign in" under it
   - Before: "Pick how the agent should think." (the options are ways to pay for or connect the agent)
   - After: "Sign up to use the AI agent, or skip it and just slice."
3. **Describing the mechanism instead of the action.** Say what the person does, not how the screen works inside.
   - Before: "The sixth digit signs you in. Read the email on any device."
   - After: "Type the code from the email. You can open it on your phone."
4. **Internal labels.** A name the team uses, shown nowhere else on the screen, explains nothing.
   - Before: "An Expert message is 5 credits."
   - After: the cost in things the person does, for example "500 credits is about 100 questions to the agent" (check the number against real costs)
5. **Hinting at a consequence instead of stating it.** If a choice changes what the person gets, say what it changes.
   - Before: "Skip, and JusPrin slices for a generic 0.4 mm printer until you add yours."
   - After: "Until you add yours, files are sliced for a 250 mm Klipper printer and may not print on other printers."

Space on a screen is tight, and short text looks finished. That is how these lines get written. Shorten only after the full sentence passes the cold-reader check, and check again after shortening.
