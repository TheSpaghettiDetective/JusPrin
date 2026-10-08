// The setup page: everything the setup card counts but has no room for. The
// presets in full, each value that differs from them under the page and group
// it has in OrcaSlicer's settings, each object's own overrides, and each
// plate's estimate. It replaces the chat body, so the card in the chat never
// has to grow. Like the card, it presents facts the host read; a saved chat's
// page is that chat's saved summary, never today's project.

import { ReactNode, useState } from 'react';
import { AppliedSetupInfo, ChangeInfo, PresetDeltaInfo, SetupIdentityInfo, WorkspaceContext } from '../bridge/protocol';
import { AgentPaneToggle } from './ChatNavigation';
import { ChangeRows, formatGrams, formatPrintTime, orderedPresetDeltas, setupPresentation } from './SetupCard';

interface SetupPageProps {
  context: WorkspaceContext;
  historical?: boolean;
  working?: boolean;
  agentAvailable?: boolean;
  changes?: ChangeInfo[];
  now?: number;
  sent?: { at: string; printer: string };
  savedAt?: string;
}

type LocalOverride = AppliedSetupInfo['localOverrides'][number];

function Row({ label, children }: { label: string; children: ReactNode }) {
  return <div className="current-setup-row"><dt>{label}</dt><dd>{children}</dd></div>;
}

function Presets({ context }: { context: WorkspaceContext }) {
  const identity = context.setupIdentity;
  const process = context.printer.process.trim();
  const filaments = identity?.filaments ?? [];
  if (!identity?.printer && !process && filaments.length === 0 && !identity?.plateType) return null;
  return <><hr className="current-setup-divider" /><section className="current-setup-section">
    <h3>Presets</h3>
    <dl className="current-setup-rows">
      {identity?.printer && <Row label="Printer">{identity.printer}</Row>}
      {process && <Row label="Process">{process}</Row>}
      {filaments.map((filament, index) => <Row key={index} label={filaments.length > 1 ? `Filament ${index + 1}` : 'Filament'}>
        {filament.preset || filament.material || 'Unavailable'}</Row>)}
      {identity?.plateType && <Row label="Build plate">{identity.plateType}</Row>}
    </dl>
  </section></>;
}

// Consecutive runs that share a heading, in the order they first appear.
function runs<T>(items: T[], heading: (item: T) => string): [string, T[]][] {
  const groups = new Map<string, T[]>();
  for (const item of items) groups.set(heading(item), [...(groups.get(heading(item)) ?? []), item]);
  return [...groups];
}

const PRESET_ORDER = ['process', 'filament', 'printer'];

function PresetChanges({ deltas, changes }: { deltas: PresetDeltaInfo[]; changes?: ChangeInfo[] }) {
  // The card's own order inside a preset, so its two rows lead here too; the
  // presets themselves keep one order whatever was edited last.
  const ordered = orderedPresetDeltas(deltas, changes);
  const rank = (delta: PresetDeltaInfo) => {
    const index = PRESET_ORDER.indexOf(delta.presetType ?? 'process');
    return index < 0 ? PRESET_ORDER.length : index;
  };
  const byPreset = runs([...ordered].sort((left, right) => rank(left) - rank(right)),
    (delta) => `${delta.presetType ?? ''}\u0000${delta.presetName ?? ''}`);
  return <><hr className="current-setup-divider" /><section className="current-setup-section">
    <h3>{ordered.length > 0 ? `Changed from presets · ${ordered.length}` : 'Changed from presets'}</h3>
    {ordered.length === 0 && <p className="current-setup-note">No changes from presets</p>}
    {byPreset.map(([preset, inPreset]) => <PresetGroup key={preset} name={inPreset[0].presetName ?? ''} deltas={inPreset} />)}
  </section></>;
}

function PresetGroup({ name, deltas }: { name: string; deltas: PresetDeltaInfo[] }) {
  return <>
    {name && <p className="current-setup-object-name">{name}</p>}
    {runs(deltas, (delta) => [delta.page, delta.group].filter(Boolean).join(' · ')).map(([location, rows]) =>
      <Group key={location} heading={location}><ChangeRows deltas={rows} /></Group>)}
  </>;
}

function Group({ heading, children }: { heading: string; children: ReactNode }) {
  return <>{heading && <p className="current-setup-group">{heading}</p>}{children}</>;
}

const sentence = (text: string) => {
  const words = text.replace(/[_-]+/g, ' ').trim();
  return words.charAt(0).toUpperCase() + words.slice(1);
};

// What an override is on, under its object's own name. A modifier is
// "bracket.stl / Mounting tabs", and the volume's name is enough; paint or a
// height range is on the object itself and is named by its kind.
function overrideHeading(local: LocalOverride): string {
  if (local.kind === 'object') return 'Whole object';
  const volume = local.target.indexOf(' / ');
  return volume < 0 ? sentence(local.kind) : `${local.target.slice(volume + 3)} · ${local.kind}`;
}

function overrideValue(local: LocalOverride, identity: SetupIdentityInfo | null | undefined): string {
  // OrcaSlicer stores an object's filament as a slot number. This is the one
  // setting the page knows by its key: the number alone says nothing, and the
  // slot's preset is a fact the page already has.
  if (local.key === 'extruder') {
    const slot = Number(local.value);
    const filament = Number.isInteger(slot) && slot > 0 ? identity?.filaments[slot - 1] : undefined;
    if (filament) return `${slot} · ${filament.preset || filament.material}`;
  }
  return local.display || local.value;
}

function ObjectOverrides({ object, name, locals, identity, open, onToggle }: { object: number; name: string;
  locals: LocalOverride[]; identity: SetupIdentityInfo | null | undefined; open: boolean; onToggle: () => void }) {
  if (locals.length === 0) return <div className="current-setup-object">
    <p className="current-setup-object-name">{name}</p>
    <p className="current-setup-note">Uses plate settings</p>
  </div>;
  const targets = runs(locals, overrideHeading);
  return <>
    <button type="button" className="current-setup-disclosure" aria-expanded={open} aria-controls={`setup-object-${object}`}
      onClick={onToggle}>
      <span className={`jp-icon jp-icon-chevron-${open ? 'down' : 'right'}`} aria-hidden="true" />
      {name} · {locals.length} {locals.length === 1 ? 'override' : 'overrides'}
    </button>
    {open && <div className="current-setup-object-rows" id={`setup-object-${object}`}>
      {targets.map(([heading, rows]) => {
        // Paint, a blocker, a variable-height profile: a customization with
        // no value to quote is named by its heading alone.
        const valued = rows.filter((local) => local.key && (local.display || local.value));
        return <Group key={heading} heading={heading}>
          {valued.length > 0 && <dl className="current-setup-rows">{valued.map((local, index) => {
            // A summary saved before the host named settings has the key.
            const name = local.label || sentence(local.key);
            return <div className="current-setup-row current-setup-row--change" key={`${local.key}-${index}`}>
              <dt title={name}>{name}</dt><dd>{overrideValue(local, identity)}</dd>
            </div>;
          })}</dl>}
        </Group>;
      })}
    </div>}
  </>;
}

function Objects({ context }: { context: WorkspaceContext }) {
  const names = context.appliedSetup?.objects ?? [];
  const locals = context.appliedSetup?.localOverrides ?? [];
  const of = (object: number) => locals.filter((local) => local.object === object);
  // The first object that has overrides starts open: the page was opened to
  // read them.
  const [open, setOpen] = useState<Set<number>>(() => new Set(names.map((_, object) => object)
    .filter((object) => of(object).length > 0).slice(0, 1)));
  if (names.length === 0) return null;
  const plate = context.plates.find((item) => item.active)?.name;
  return <><hr className="current-setup-divider" /><section className="current-setup-section">
    <h3>{plate ? `Objects on ${plate}` : 'Objects'} · {names.length}</h3>
    {names.map((name, object) => <ObjectOverrides key={object} object={object} name={name} locals={of(object)}
      identity={context.setupIdentity} open={open.has(object)} onToggle={() => setOpen((before) => {
        const next = new Set(before);
        if (!next.delete(object)) next.add(object);
        return next;
      })} />)}
  </section></>;
}

function plateFacts(context: WorkspaceContext, index: number): string {
  const plate = context.plates[index];
  const facts: string[] = [];
  const estimate = plate.sliced ? plate.estimate : null;
  if (estimate && (estimate.timeAvailable ?? estimate.printTimeSeconds > 0)) facts.push(formatPrintTime(estimate.printTimeSeconds));
  if (estimate && (estimate.materialAvailable ?? estimate.materialGrams > 0)) facts.push(formatGrams(estimate.materialGrams));
  if (facts.length === 0) facts.push(plate.sliced ? 'Estimate unavailable' : 'Not sliced');
  // Spiral vase is the plate's own switch, not a difference from a preset,
  // and the host reads it for the plate on show.
  if (plate.active && context.appliedSetup?.spiralMode) facts.push('Spiral vase');
  return facts.join(' · ');
}

function Plates({ context }: { context: WorkspaceContext }) {
  if (context.plates.length === 0) return null;
  return <><hr className="current-setup-divider" /><section className="current-setup-section">
    <h3>Plates</h3>
    <dl className="current-setup-rows">{context.plates.map((plate, index) =>
      <Row key={plate.id} label={plate.active && context.plates.length > 1 ? `${plate.name} · shown` : plate.name}>
        {plateFacts(context, index)}</Row>)}</dl>
  </section></>;
}

// The page's own header bar: the way back to the chat, and the same control
// the chat's header has for putting the whole panel away. Back takes the focus
// the "View setup" link had, so the keyboard is never left on nothing.
export function SetupPageHeader({ onBack, onCollapse }: { onBack: () => void; onCollapse: () => void }) {
  return <header className="chat-header current-setup-page-header">
    <button type="button" className="panel-link-button panel-back-link" autoFocus onClick={onBack}>
      <span className="jp-icon jp-icon-chevron-left" aria-hidden="true" /> Back
    </button>
    <AgentPaneToggle onCollapse={onCollapse} />
  </header>;
}

export function SetupPage({ context, historical, working, agentAvailable, changes, now, sent, savedAt }: SetupPageProps) {
  const presentation = setupPresentation(context, { historical, working, agentAvailable, changes, now, sent, savedAt });
  return <main className="current-setup-page" data-testid="current-setup-page">
    <section className={presentation.live ? 'current-setup current-setup--live' : 'current-setup'}
      aria-label={historical ? 'Earlier setup — saved with this conversation' : 'Current setup'}>
      <div className="current-setup-header"><span className="current-setup-label">{presentation.label}</span>
        {presentation.state && <span className="current-setup-state">{presentation.state}</span>}</div>
      {presentation.title && <h2 className="current-setup-title" title={presentation.title}>{presentation.title}</h2>}
      <Presets context={context} />
      <PresetChanges deltas={context.presetDeltas} changes={changes} />
      <Objects context={context} />
      <Plates context={context} />
    </section>
  </main>;
}
