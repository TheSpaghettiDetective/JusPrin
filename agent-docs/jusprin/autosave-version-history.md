# Local autosave and version history

JusPrin saves authored projects in the application data directory under
`jusprin/projects/<project-id>/`. The project ID is independent of the source
filename. Opening an external 3MF or model imports it into a managed draft;
autosave never overwrites the source. **Saved** means the printable
project checkpoint and the current JusPrin document are durable. A 3MF is written
to a user-selected path only through **Export project 3MF…**. A copied 3MF does
not include local versions, conversations, or attachments.

The project header's actions menu exposes the save state, version history,
recent local projects, and export. Version history restores an earlier model
and its effective project settings. The native window title shows the managed
project name and the same save state as the header. Orca's dirty asterisk
does not represent whether the local project has been saved. Before restoring,
JusPrin saves and pins
the current version, then records the restore as a new version, so the prior
state remains available. Each older timestamp has a separate Restore button;
clicking the timestamp does nothing, and Restore asks for confirmation. A blank authored project gets one empty baseline
version so it can be reopened; later document edits do not add versions. A
first Agent model operation pins that baseline as its before-version.
Conversation, intent, plan, region descriptions,
audit, and completed print records remain in the current document. Restoring a
model does not undo physical prints or change global printer facts. Agent
mutations save and pin their actual
before-state, then link an after-version and outcome in an operation record.

## Storage and recovery contract

The restoration rules above describe ordinary Version history. **Restore and
resume** is a separate chat transition. Each active chat keeps a checkpoint
with a model-version reference, matching print intent, plan and region
annotations, and a structured setup summary. Browsing an older chat does not
change the working project. Resuming it saves and pins the outgoing state,
restores the model and matching planning fields, and activates that chat only
after the new document is durable. Other conversations, audit and print
records, and global printer facts remain. This does not add a full project-
document copy to model versions or make document edits create model versions.
Ordinary Version history remains model-only and does not activate an older
chat. An interrupted chat transition leaves a durable pending marker and
blocks that managed project for explicit recovery on restart.

Each model version has Orca backup-format metadata and a manifest of object
resources. It has no copy of the project document. Unchanged object resources are referenced rather
than serialized again. Changed objects are serialized with Orca's split-model
writer into immutable ZIP resources. A renamed object can reuse its resource;
materialization relabels its entry for Orca's native restore loader. Metadata
and each resource are checked for required entries and geometry. The live
Orca backup directory and an imported source file are never version-store
dependencies.

All project records advance one `current-state.json` document without creating
a model version. The document is atomically replaced after 1.5 seconds without
a change, or within 15 seconds during a continuous stream. Agent and project
boundaries wait for the write. Attachments are written once before their
document references become durable; operation records linking before/after
versions are committed once at the operation boundary. A model checkpoint
ensures the current document is durable before it publishes a new `HEAD`.

## Save classification

The autosave controller observes two independent kinds of change. Native
geometry, transforms, paint, plates, and effective project settings can change
what an earlier printable project reconstructs and therefore create a model
version. Document changes, including streamed Agent messages, drafts, intent,
plans, region descriptions, audit records, and print facts, update only the
current document. `docRevision` is a signal to coalesce that write; neither
it nor document contents participate in model-version equality. A workspace
event can prompt a model check, but cannot make unchanged checkpoint content
into a new version.

A single writer owns each project through an OS file lock. It writes and
flushes the current document and changed resources before publishing a version directory,
then atomically replaces `HEAD` with the ordered committed-version index.
POSIX also synchronizes containing directories; Windows flushes individual
files and uses `MoveFileExW` with `MOVEFILE_WRITE_THROUGH`. Restart discards
unfinished staging, unindexed versions, unreachable resources, and old
disposable restore directories. If saving
fails, the current model stays open, the header reports a local save failure,
and the next save retries it. Project replacement is blocked until the live
state is saved. Explicit export validates a temporary 3MF before replacing
its destination.

Older projects may still contain `Auxiliaries/JusPrin/state.json` and a
recovery mirror. On first adoption, JusPrin reads that state and copies its
attachments to the managed store; it does not delete the old source. Once a
managed version exists, local state is authoritative. Reopening a copied
external 3MF with an already managed identity forks a new project identity.
When an external 3MF's source path matches a saved project, JusPrin asks
whether to create another project or open the existing one directly. If several
projects match, it continues the current one when it came from that file;
otherwise it opens the most recently updated match. The path
match is a reminder, not a content identity check: a changed file at the same
path still offers the choice.
Earlier local versions with `state.json` and a model-bound document sidecar
remain readable; the first new document write upgrades them to the independent
current-document format without rewriting historical versions.
Recent projects appear on Home and in the header menu. Selecting one
opens its managed version even when its imported source file has been removed.
The most recently authored local project resumes on startup.

## Chosen defaults and limits

- The controller checks a lightweight native state signature every 2 seconds
  and waits 1.5 seconds after an observed model edit before starting a background
  model save. Document changes use the coalesced state write described above;
  they never call Orca's 3MF exporter. An unchanged project does not run the
  exporter. Project replacement and Agent boundaries save synchronously.
- While the JusPrin shell owns managed autosave, Orca's automatic backup timer
  and object-mesh backup cache are suspended. The configured backup interval is
  left intact and resumes if the shell is detached. Manual 3MF export remains
  available.
- Retention aims for at most 200 versions and 4 GiB per project. These are
  soft limits: the current version, pinned versions, and Agent operation
  references survive even when they exceed the budget. Unreferenced object
  resources and attachments are removed after the retained index is durable.
- A support, seam, color, or fuzzy paint edit can serialize the whole painted
  object because Orca stores triangle paint with its mesh. The coherence
  spike measured about 31 MB and 6.7 seconds of worker time for a large paint
  edit; this is a fixture measurement, not a latency guarantee.
- History is local to this application data directory. There is no portable
  history export or automatic cloud synchronization.

## Verification

The native workspace harness checks the current document across historical
model restores, selective resources, all four paint
channels, empty restore, corrupt and missing resource rejection, export
replacement, locks, retention, and attachment reachability. The shell harness
checks streamed replies without per-word model exports, legacy adoption,
failed-save behavior, restore, restart, and Agent
boundaries. Separate process tests stop publication after resources, version,
and `HEAD`, then verify recovery in a fresh process. These checks run on macOS;
Windows and Linux runtime behavior still needs platform verification before
those builds are released.
