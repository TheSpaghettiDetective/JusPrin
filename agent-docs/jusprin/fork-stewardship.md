# Fork stewardship

**Status:** Required discipline for every change to an upstream-owned file on `jusprin-newui`.

JusPrin is a long-lived fork. Every line it changes in an OrcaSlicer-owned file is a line it re-resolves at every rebase, forever, by a person who was not there when the line was written. The rules below exist to keep that permanent cost small and, where it cannot be small, to keep it visible.

One question generates all of them: **after this change, what does the fork own?**

- Owning *product policy* is safe. It is ours, upstream will never write it, and it lives in our files.
- Owning a *small neutral seam* is acceptable. It is the kind of thing upstream might plausibly have written itself, so it survives refactors and could be contributed back.
- Owning *upstream's behavior* is the expensive kind. It commits us to re-deriving upstream's intent forever, and the day we take ownership is usually the day the behavior quietly diverges.

Two mechanical facts drive everything else. Git merges text, not meaning: it conflicts when two sides touch the same or adjacent lines, and it stays silent when they touch different lines even if the result is nonsense. And a conflict is a *stop* — a human is forced to look — while silent divergence is unbounded and invisible. Conflicts are therefore not the enemy. Unnoticed drift is, and conflicts that resolve wrongly are.

## The ten principles

### 1. Conflict probability is about where you write, not how much

**Rule.** Place fork edits where upstream rarely writes, and keep the number of distinct sites low.

**Mechanism.** Risk is roughly *how often upstream edits this region* times *how much of the region you occupy*, summed over every place you touched. Each site is an independent draw. New files have probability zero, so their size is free; one line inside a function upstream is actively reworking is expensive.

**The test.** How many separate places does this change touch, and how busy is each one? Would one seam deliver the same capability?

**Violation.** Delivering a single capability through a dozen scattered notification lines across several upstream files, when one funnel that every path already passes through would have carried them.

### 2. A clean merge is not a safe merge

**Rule.** Prefer edits that are locally correct over edits that participate in upstream's control flow.

**Mechanism.** Git merges text. If your edit's correctness depends on the code around it — an ordering, a guard, an early return, an invariant nobody wrote down — upstream can restructure the surroundings, the merge succeeds, and your edit now runs under assumptions that no longer hold, with no signal at all.

**The test.** If upstream restructures the code around this hunk, do I get a conflict or silence? If silence, would any test catch it?

**Violation.** Moving a member out of a constructor's initializer list because a callback can fire mid-construction. Nothing in upstream's code records that invariant, so a future cleanup restores the old form with no conflict and no error.

### 3. Add to upstream's code; do not revise it

**Rule.** Append, insert, and call. Do not rewrite upstream functions or change their signatures.

**Mechanism.** An additive hook fails gracefully: when it conflicts, the resolution is "keep both," and anyone can do that correctly. A rewritten function fails badly: every future upstream change to it is a conflict whose resolution requires re-deriving two intents at once, in code no longer shared with upstream. Rewriting subtle logic also tends to change it, and you find out later.

**The test.** Does upstream's line survive my change? If I deleted or replaced one, why could I not have added next to it?

**Violation.** Replacing OrcaSlicer's undo and redo implementations with fork-owned equivalents in order to learn whether the history position moved. Re-expressing the vendor-specific snapshot walk silently dropped one of its branches. An additive helper that reports the result, leaving upstream's functions untouched, would have answered the same question.

### 4. Own the policy; borrow the mechanism

**Rule.** No product name, color, dimension, or product-specific decision in an upstream-owned file. Upstream files may gain small neutral capabilities; the policy that uses them lives in fork-owned code.

**Mechanism.** Product specifics in upstream files are permanent divergence: they can never be upstreamed and never dropped, so they must be re-resolved forever. A neutral capability — a flag, a getter/setter pair, an event — reads like something upstream could have written, which is exactly why it survives refactors.

**The test.** Would this addition look reasonable in an upstream pull request, with no explanation of our product?

**Violation.** A product-named enum value inside a rendering class, or product decisions embedded in an upstream mouse handler. The fix is a neutral options struct plus a fork-owned controller that applies our policy.

### 5. One seam beats many touch points

**Rule.** Find the single place every path already passes through, and put the fork's line there.

**Mechanism.** Each touch point is an independent conflict draw and an independent thing a future resolver must understand. Collapsing many small recurring costs into one is worth some indirection.

**The test.** Do all the paths I am hooking already converge somewhere downstream? Is there one function that runs exactly when the thing I care about has happened?

**Violation.** Publishing a history-changed event from undo, from redo, and from both history-navigation entry points, when all four funnel through a single update function that runs only on a successful move.

### 6. One shared implementation is worth a guaranteed conflict

**Rule.** When the alternative is two copies of the same behavior, keep the shared path and accept that it will conflict.

**Mechanism.** A conflict is a stop; drift between two copies is silent and unbounded. Trading an invisible risk for a visible, small, recurring one is a good trade.

**The test.** Where did the logic I removed actually go? A move has one implementation; only a duplication risks divergence. If I wrote new behavior beside an existing upstream path rather than calling it, upstream's future improvements will never reach our users.

**Violation.** Writing a second object-duplication routine next to OrcaSlicer's clipboard path and pointing an existing dialog at it. Reusing the existing path — extending it where it was too narrow — keeps one implementation.

### 7. Unavoidable conflicts must be easy to resolve correctly

**Rule.** When you must delete or relocate an upstream line, say at the site what moved, where it went, and what a future rebase must carry across.

**Mechanism.** The real cost of a conflict is not the stop; it is the chance of resolving it wrongly. Appending near an upstream line yields a conflict whose obvious resolution — keep both — is right. Deleting an upstream line yields a conflict whose obvious resolution — take mine, the logic moved — silently discards whatever upstream did to that line in the meantime.

**The test.** Six months from now, is the natural resolution of the conflict I am creating the correct one? If not, what does the resolver need to read to get it right, and is that text next to the conflict?

**Violation.** Removing an upstream snapshot label that upstream later localized. Git stops for a human, but the intent-faithful resolution throws the translation away, and nothing at the site says otherwise.

### 8. Do not diverge where you gain nothing; converge where upstream already went

**Rule.** No gratuitous formatting, reordering, or whitespace differences. Before reverting a cosmetic difference, compare against current upstream — it may have made the same change.

**Mechanism.** Every valueless difference is a conflict site that buys nothing. And if upstream has independently arrived at your version, "fixing" it back creates divergence rather than removing it. Adopting a change upstream has already made converges the two trees early and makes the eventual conflict resolve itself.

**The test.** Does this difference carry meaning? Has upstream already made it?

**Violation.** Consuming a shared blank line in a list of build subdirectories, where upstream also appends entries.

### 9. Verify the risk claim instead of reasoning about it

**Rule.** Before asserting that a change is or is not rebase-risky, check it. Three checks, each one command: does git actually conflict there; has upstream already converged on the same change; is the change a move or a duplication.

**Mechanism.** A coherent story about fork risk is not a true one, and every one of these is cheaper to check than to argue about. Reasoning alone reliably produces confident, wrong claims — that a lost change is "silent" when git stops for it, that a move is a "duplication," that a cosmetic difference is gratuitous when upstream shares it.

**The test.** Did I run the simulation, or am I describing what I expect it to say?

**Violation.** A report that lists which files are rebase-sensitive and calls the change narrow, without having run the merge simulation that would have found two lossy resolutions waiting in it.

### 10. Pull direction dominates every local edit

**Rule.** Replay the fork's own commits onto upstream. Never merge upstream in, and never rebase the whole branch.

**Mechanism.** The fork's own footprint is small; the inherited release-branch history is not. Merging upstream in makes the conflict surface the entire divergence between the release branch and upstream's development line — none of it ours. Rebasing only our commits discards that inherited history by taking upstream's version of every file we never touched.

**The test.** Am I replaying our commits onto upstream, or bringing upstream into ours?

**Violation.** `git merge upstream/main`, which produces a conflict list dominated by vendor profiles, translations, and CI files that JusPrin has never edited.

## Before you finish

A change that touches upstream-owned files is not done until this is recorded in the handback or pull request:

1. **The rebase simulation and its result.** Run it against current upstream and name both endpoint commits — measurements are not reproducible otherwise.

   ```bash
   git merge-tree --write-tree --merge-base=<fork base> upstream/main HEAD
   ```

   For each conflicted file, state whether the natural resolution is correct or lossy. A lossy one is a defect to fix now, not a note for later.

2. **An account of every upstream line deleted or rewritten.** Where the logic went, and why an additive form was not possible.

3. **A statement of what the fork now owns.** Policy, a neutral seam, or upstream behavior. The third needs an explicit justification.

An automatic merge is evidence about today's text. It is not proof that a future rebase will merge, and never proof that the merged result behaves correctly. After every upstream update, run the focused contract tests, the full native build, the real multi-plate workflow, native gizmo input, and the stock-behavior regression checks.

## This fork's terrain

Facts specific to OrcaSlicer, worth knowing before choosing where to put a seam. They change over time; re-measure rather than trusting these.

`Plater.cpp` is by far the busiest file the fork touches, followed by the central `src/slic3r/CMakeLists.txt`, `GLCanvas3D.cpp`, and `PartPlate.cpp`. `GLToolbar` and `GLGizmosManager` are quiet. File-level churn is only a first approximation: some functions inside `Plater.cpp` have not been touched upstream in the entire life of this fork, and a seam there is far cheaper than the file's overall rate suggests. Measure the function, not just the file.

Some native state is shared rather than owned by one object. OrcaSlicer's collapse toolbar belongs to `Plater` and is used by both the Prepare and Preview canvases, so a per-canvas guard cannot save and restore it correctly. Shared state needs one owner at the shared lifetime boundary, and exact restoration needs a getter as well as a setter.

Historical measurements and the incidents behind several of these rules are in the [OrcaSlicer integration guide](orca-integration-guide.md) and, for the spike era, in the [POC reference](poc-reference.md).
