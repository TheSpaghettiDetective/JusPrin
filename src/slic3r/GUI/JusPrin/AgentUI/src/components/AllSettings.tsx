// The All settings panel: every setting in force, in the Agent pane's place,
// opened from the setup card's "All settings" link (the "JusPrin v2" canvas,
// page "Current Setup Card", the four "All settings" boards). The host reads
// the table (settings_catalog); this page only arranges it. Showing opens on
// the whole project and then follows the selection in the 3D view: one plate
// when the project has more than one, or one object, for which only what it
// sets itself is listed. That view is the only one with anything to press:
// "Use project value" under a setting the object, a part, a modifier or a
// height range sets itself, and "Remove" under its support blockers or
// enforcers, each where the host's table offers it (settings_action).

import { useEffect, useRef, useState } from 'react';
import { CatalogFilamentInfo, CatalogObjectInfo, CatalogOverrideInfo, CatalogPlateInfo, CatalogSettingInfo,
  SettingsActionPayload, SettingsCatalogPayload, WorkspaceContext } from '../bridge/protocol';
import { ActionMenu, AgentPaneToggle, MenuItem } from './ChatNavigation';

export type SettingsScope = { kind: 'project' } | { kind: 'plate'; id: string } | { kind: 'object'; id: string };
export type SettingsFilter = 'all' | 'changed';

// OrcaSlicer's process tab pages, in its order. A setting the tab does not
// index (empty page) is filed under Others.
export const PROCESS_PAGES = ['Quality', 'Strength', 'Speed', 'Support', 'Multimaterial', 'Others'];
// Side by side up to here; beyond it one slot at a time, from a picker.
export const COLUMN_SLOTS = 3;

export interface AllSettingsProps {
  context: WorkspaceContext;
  // Null while the host is reading it. A saved chat has no live table: pass
  // catalogFromContext(summary) and `saved`.
  catalog: SettingsCatalogPayload | null;
  saved?: boolean;
  // The host cannot read the table (an older build, or a non-FFF printer
  // reports it itself through `available`).
  unsupported?: boolean;
  // What Showing opens on; the whole project unless a fixture says otherwise.
  initialScope?: SettingsScope;
  // Sends a press on an object row's link; absent where the host takes none
  // (an older build), and a saved chat offers none.
  onAction?: (action: SettingsActionPayload) => void;
}

function plural(count: number, one: string, many: string): string {
  return `${count} ${count === 1 ? one : many}`;
}

function shortPreset(name: string): string {
  const separator = name.indexOf(' @');
  return separator < 0 ? name : name.slice(0, separator);
}

const sentence = (text: string) => {
  const words = text.replace(/[_-]+/g, ' ').trim();
  return words.charAt(0).toUpperCase() + words.slice(1);
};

// The volume an override is on ("bracket.stl / Mounting tabs" names the
// modifier), or nothing for the object itself.
function volumeName(local: CatalogOverrideInfo): string | undefined {
  const separator = local.target.indexOf(' / ');
  return separator < 0 ? undefined : local.target.slice(separator + 3);
}

// Which object the 3D view has: exactly one, or none to follow.
export function selectedObject(context: WorkspaceContext): string | null {
  const selection = context.selection;
  return selection.status === 'objects' && selection.objectIds.length === 1 ? selection.objectIds[0] : null;
}

// A saved chat's table, from the summary the chat saved: the settings that
// differed from the presets then, and the objects' own settings. The rest of
// the table was not saved, so the panel shows these as the changed ones.
export function catalogFromContext(context: WorkspaceContext): SettingsCatalogPayload {
  // "0.2 → 0.28 mm" says the unit once; each side gets it back.
  const split = (display: string): [string, string] => {
    const arrow = display.indexOf(' → ');
    if (arrow < 0) return [display, display];
    const before = display.slice(0, arrow);
    const after = display.slice(arrow + 3);
    const unit = after.match(/^-?[\d.,]+ (\D\S*)$/);
    return [unit && /^-?[\d.,]+$/.test(before) ? `${before} ${unit[1]}` : before, after];
  };
  const rows = (type: string): CatalogSettingInfo[] => context.presetDeltas
    .filter((delta) => (delta.presetType ?? 'process') === type)
    .map((delta) => {
      const [presetDisplay, display] = delta.display ? split(delta.display) : [delta.preset, delta.value];
      return { key: delta.key, label: delta.label || delta.key, page: delta.page ?? '', group: delta.group ?? '',
        value: delta.value, display, preset: delta.preset, presetDisplay };
    });
  const identity = context.setupIdentity;
  const plate = context.plates.find((item) => item.active);
  const applied = context.appliedSetup;
  return {
    available: true,
    process: { name: context.printer.process, settings: rows('process') },
    printer: { name: identity?.printer ?? context.printer.preset, settings: rows('printer') },
    filaments: (identity?.filaments ?? []).map((filament, index) => ({ slot: index + 1, preset: filament.preset,
      material: filament.material, colour: '', settings: index === 0 ? rows('filament') : [] })),
    plates: plate ? [{ id: plate.id, name: plate.name, active: true, bedType: identity?.plateType ?? '', ownBedType: false,
      bedTempKey: '', bedTempInitialKey: '', own: [] }] : [],
    objects: (applied?.objects ?? []).map((name, index) => ({
      id: plate?.objects.find((item) => item.name === name)?.id ?? `saved-${index}`, name, plateId: plate?.id,
      overrides: (applied?.localOverrides ?? []).filter((local) => local.object === index)
        .map(({ target, kind, key, value, label, display }) => ({ target, kind, key, value, label, display })) })),
  };
}

const lower = (text: string) => text.toLowerCase();

function matches(setting: { label: string; key: string }, query: string): boolean {
  const wanted = lower(query.trim());
  return !wanted || lower(setting.label).includes(wanted) || lower(setting.key).includes(wanted);
}

const changed = (setting: CatalogSettingInfo) => setting.preset !== undefined;

// Consecutive runs that share a heading, in the order they first appear.
function runs<T>(items: T[], heading: (item: T) => string): [string, T[]][] {
  const groups = new Map<string, T[]>();
  for (const item of items) groups.set(heading(item), [...(groups.get(heading(item)) ?? []), item]);
  return [...groups];
}

const pageOf = (setting: CatalogSettingInfo) => PROCESS_PAGES.includes(setting.page) ? setting.page : 'Others';

// -- Rows ---------------------------------------------------------------------

interface RowAction { text: string; name: string; onPress: () => void }

// A row's link sits at the end of its last note, under the value.
function Row({ label, value, bold, muted, notes, tag, action }: {
  label: string; value: string; bold?: boolean; muted?: boolean; notes?: string[]; tag?: string; action?: RowAction;
}) {
  const shown = (notes ?? []).filter(Boolean);
  const last = action ? shown.pop() ?? '' : undefined;
  return <div className={bold ? 'all-settings-row all-settings-row--changed' : 'all-settings-row'}>
    <div className="all-settings-row-line">
      <span className="all-settings-row-name" title={label}>{label}</span>
      <span className={['all-settings-row-value', bold && 'all-settings-row-value--bold', muted && 'all-settings-row-value--muted']
        .filter(Boolean).join(' ')}>{value}</span>
      {tag && <span className="all-settings-row-tag">{tag}</span>}
    </div>
    {shown.map((note) => <p className="all-settings-row-note" key={note}>{note}</p>)}
    {action && <div className="all-settings-row-foot">
      <p className="all-settings-row-note">{last}</p>
      <button type="button" className="link all-settings-row-action" aria-label={action.name} onClick={action.onPress}>{action.text}</button>
      {tag && <span className="all-settings-row-tag" aria-hidden="true" />}
    </div>}
  </div>;
}

// A group's header: a disclosure when it has rows, a plain line that says
// "same as project" (or whatever `summary` says) when it has none.
function Group({ name, summary, open, onToggle, children, count }: {
  name: string; summary: string; open: boolean; onToggle: () => void; children?: React.ReactNode; count: number;
}) {
  if (count === 0)
    return <div className="all-settings-group"><div className="all-settings-group-header all-settings-group-header--empty">
      <span className="jp-icon jp-icon-chevron-right" aria-hidden="true" />
      <span className="all-settings-group-name">{name}</span>
      <span className="all-settings-group-summary">{summary}</span>
    </div></div>;
  return <div className="all-settings-group">
    <button type="button" className="all-settings-group-header" aria-expanded={open} onClick={onToggle}>
      <span className={`jp-icon jp-icon-chevron-${open ? 'down' : 'right'}`} aria-hidden="true" />
      <span className="all-settings-group-name">{name}</span>
      <span className="all-settings-group-summary">{summary}</span>
    </button>
    {open && <div className="all-settings-group-rows">{children}</div>}
  </div>;
}

// Rows of one preset under their Orca groups, as a name and a value; a
// changed value is bold with the preset's under it.
function PresetRows({ settings, notes, byPage }: {
  settings: CatalogSettingInfo[]; notes?: (setting: CatalogSettingInfo) => string[]; byPage?: boolean;
}) {
  return <>{runs(settings, (setting) => byPage ? [setting.page, setting.group].filter(Boolean).join(' · ') : setting.group)
    .map(([heading, rows]) => <div className="all-settings-run" key={heading || '(ungrouped)'}>
      {heading && <p className="all-settings-run-name">{heading}</p>}
      {rows.map((setting) => <Row key={setting.key} label={setting.label} bold={changed(setting)}
        muted={setting.inherited} value={setting.inherited ? 'printer' : setting.display}
        notes={[changed(setting) ? `Preset: ${setting.presetDisplay ?? setting.preset}` : '',
          setting.inherited ? `From the printer: ${setting.inheritedDisplay}` : '', ...(notes?.(setting) ?? [])]} />)}
    </div>)}</>;
}

// -- The Filament group ---------------------------------------------------------

function Swatch({ colour, label }: { colour: string; label: string }) {
  return <span className="all-settings-swatch" role="img" aria-label={label} title={label}
    style={colour ? { background: colour } : undefined} />;
}

// The filament rows, with the bed temperature named for the plate type the
// shown plate prints with.
function filamentRows(filament: CatalogFilamentInfo, plate: CatalogPlateInfo | undefined, query: string,
  filter: SettingsFilter): CatalogSettingInfo[] {
  return filament.settings.filter((setting) => matches(setting, query) && (filter === 'all' || changed(setting)))
    .map((setting) => plate?.bedType && (setting.key === plate.bedTempKey || setting.key === plate.bedTempInitialKey)
      ? { ...setting, label: `${setting.label} · ${plate.bedType}` } : setting);
}

// Two or three slots side by side: one column per slot, the rows being slot
// 1's settings in order. A value inherited from the printer reads "printer";
// a filament's own override, or a change from its preset, is bold with a
// note under the row.
function FilamentColumns({ filaments, plate, query, filter }: {
  filaments: CatalogFilamentInfo[]; plate?: CatalogPlateInfo; query: string; filter: SettingsFilter;
}) {
  const perSlot = filaments.map((filament) => new Map(filament.settings.map((setting) => [setting.key, setting])));
  const lead = filamentRows(filaments[0], plate, query, 'all');
  // The changed filter keeps a row some slot changed from its preset, or
  // sets itself where another slot leaves it to the printer.
  const shown = filter === 'all' ? lead : lead.filter((row) => perSlot.some((slot) => {
    const cell = slot.get(row.key);
    return !!cell && (changed(cell) || overridesPrinter(cell, perSlot, row.key));
  }));
  return <div className="all-settings-columns" style={{ '--all-settings-slots': filaments.length } as React.CSSProperties}>
    <div className="all-settings-columns-head">
      <span />
      {filaments.map((filament) => <span className="all-settings-column-slot" key={filament.slot}>
        <Swatch colour={filament.colour} label={`Slot ${filament.slot} · ${shortPreset(filament.preset)}`} />
        <span className="all-settings-column-slot-name">Slot {filament.slot}</span>
        <span className="all-settings-column-slot-material">{filament.material || shortPreset(filament.preset)}</span>
      </span>)}
    </div>
    {runs(shown, (setting) => [setting.page, setting.group].filter(Boolean).join(' · ')).map(([heading, group]) =>
      <div className="all-settings-run" key={heading || '(ungrouped)'}>
        {heading && <p className="all-settings-run-name">{heading}</p>}
        {group.map((lead) => {
          const cells = perSlot.map((slot) => slot.get(lead.key));
          const notes = cells.flatMap((cell, index) => {
            if (!cell) return [];
            const slot = filaments[index].slot;
            const lines: string[] = [];
            if (changed(cell)) lines.push(`Slot ${slot} preset: ${cell.presetDisplay ?? cell.preset}`);
            if (overridesPrinter(cell, perSlot, lead.key))
              lines.push(`Slot ${slot} overrides the printer’s ${printerValue(perSlot, lead.key)}.`);
            return lines;
          });
          const anyBold = cells.some((cell) => cell && (changed(cell) || overridesPrinter(cell, perSlot, lead.key)));
          return <div className={anyBold ? 'all-settings-row all-settings-row--changed' : 'all-settings-row'} key={lead.key}>
            <div className="all-settings-row-line all-settings-row-line--columns">
              <span className="all-settings-row-name" title={lead.label}>{lead.label}</span>
              {cells.map((cell, index) => <span key={index} className={['all-settings-row-value',
                cell && (changed(cell) || overridesPrinter(cell, perSlot, lead.key)) && 'all-settings-row-value--bold',
                cell?.inherited && 'all-settings-row-value--muted'].filter(Boolean).join(' ')}>
                {cell ? cell.inherited ? 'printer' : cell.display : '—'}</span>)}
            </div>
            {notes.map((note) => <p className="all-settings-row-note" key={note}>{note}</p>)}
          </div>;
        })}
      </div>)}
  </div>;
}

// A filament's own value for a setting another slot leaves to the printer.
function overridesPrinter(cell: CatalogSettingInfo, perSlot: Map<string, CatalogSettingInfo>[], key: string): boolean {
  return !cell.inherited && perSlot.some((slot) => slot.get(key)?.inherited);
}

function printerValue(perSlot: Map<string, CatalogSettingInfo>[], key: string): string {
  for (const slot of perSlot) {
    const cell = slot.get(key);
    if (cell?.inherited) return cell.inheritedDisplay ?? '';
  }
  return '';
}

// -- The panel ------------------------------------------------------------------

export function AllSettingsHeader({ onBack, onCollapse }: { onBack: () => void; onCollapse: () => void }) {
  return <header className="chat-header all-settings-header">
    <button type="button" className="chat-icon" aria-label="Back to chat" title="Back to chat" autoFocus onClick={onBack}>
      <span className="jp-icon jp-icon-chevron-left" aria-hidden="true" />
    </button>
    <h1>All settings</h1>
    <AgentPaneToggle onCollapse={onCollapse} />
  </header>;
}

function scopeKey(scope: SettingsScope): string {
  return scope.kind === 'project' ? 'project' : `${scope.kind}:${scope.id}`;
}

export function AllSettings({ context, catalog, saved = false, unsupported = false, initialScope, onAction }: AllSettingsProps) {
  const selected = selectedObject(context);
  const [chosen, setChosen] = useState<SettingsScope | null>(initialScope ?? null);
  // The panel opens on the whole project whatever the 3D view has. From
  // then on a change of selection there wins, and a choice made here holds
  // until the next one.
  const lastSelected = useRef(selected);
  useEffect(() => {
    if (selected === lastSelected.current) return;
    lastSelected.current = selected;
    setChosen(selected ? { kind: 'object', id: selected } : null);
  }, [selected]);
  const [query, setQuery] = useState('');
  const [filter, setFilter] = useState<SettingsFilter>(saved ? 'changed' : 'all');
  const [slot, setSlot] = useState(1);
  const [open, setOpen] = useState<Record<string, boolean>>({});

  const plates = catalog?.plates ?? context.plates.map((plate) => ({ id: plate.id, name: plate.name, active: plate.active,
    bedType: '', ownBedType: false, bedTempKey: '', bedTempInitialKey: '', own: [] as CatalogSettingInfo[] }));
  const objects: CatalogObjectInfo[] = catalog?.objects ?? context.plates.flatMap((plate) =>
    plate.objects.map((object) => ({ id: object.id, name: object.name, plateId: plate.id, overrides: [] })));
  const scope: SettingsScope = chosen && (chosen.kind === 'project' ||
    (chosen.kind === 'object' ? objects : plates).some((item) => item.id === chosen.id)) ? chosen : { kind: 'project' };
  const activePlate = plates.find((plate) => plate.active);

  const options: MenuItem[] = [
    { key: 'project', label: 'Whole project', current: scope.kind === 'project', onSelect: () => setChosen({ kind: 'project' }) },
    ...(plates.length > 1 ? plates.map((plate) => ({ key: `plate:${plate.id}`, label: plate.name,
      current: scope.kind === 'plate' && scope.id === plate.id, onSelect: () => setChosen({ kind: 'plate', id: plate.id }) })) : []),
    ...objects.map((object) => ({ key: `object:${object.id}`, label: object.name,
      current: scope.kind === 'object' && scope.id === object.id, onSelect: () => setChosen({ kind: 'object', id: object.id }) })),
  ];
  const shownName = options.find((option) => option.current)?.label ?? 'Whole project';

  const toggle = (name: string, fallback: boolean) => setOpen((before) => ({ ...before, [name]: !(before[name] ?? fallback) }));
  const isOpen = (name: string, fallback: boolean) => query.trim() ? true : open[name] ?? fallback;

  const identity = context.setupIdentity;
  const process = catalog?.process;
  const processChanges = process ? process.settings.filter(changed).length
    : context.presetDeltas.filter((delta) => (delta.presetType ?? 'process') === 'process').length;
  const objectsHere = objects.filter((object) => !activePlate || object.plateId === activePlate.id || !object.plateId);
  const withOwn = objectsHere.filter((object) => object.overrides.length > 0);

  // What the row of one process setting says about the objects that print
  // it differently: "Mounting tabs: 6".
  const overrideNotes = (setting: CatalogSettingInfo): string[] => objectsHere.flatMap((object) =>
    object.overrides.filter((local) => local.key === setting.key).map((local) =>
      `${volumeName(local) ?? object.name}: ${local.display || local.value}`));

  const body = () => {
    if (unsupported) return <p className="all-settings-note">This JusPrin build cannot list the settings.</p>;
    if (!catalog) return <p className="all-settings-note" role="status">Reading the settings…</p>;
    if (!catalog.available) return <p className="all-settings-note">No settings to show: the selected printer is not an FFF printer.</p>;
    if (scope.kind === 'object') return objectBody(objects.find((object) => object.id === scope.id)!);
    if (scope.kind === 'plate') return plateBody(plates.find((plate) => plate.id === scope.id)!);
    return projectBody();
  };

  const projectBody = () => {
    const processRows = catalog!.process.settings.filter((setting) => matches(setting, query) && (filter === 'all' || changed(setting)));
    const byPage = new Map(runs(processRows, pageOf));
    const printerRows = catalog!.printer.settings.filter((setting) => matches(setting, query) && (filter === 'all' || changed(setting)));
    const filaments = catalog!.filaments;
    const filamentShown = filaments.length > COLUMN_SLOTS ? filaments.filter((item) => item.slot === slot) : filaments;
    const filamentRowsShown = filamentShown.length === 1 ? filamentRows(filamentShown[0], activePlate, query, filter) : [];
    const filamentCount = filamentShown.length === 1 ? filamentRowsShown.length
      : filaments.length === 0 ? 0 : filamentRows(filaments[0], activePlate, query, 'all').length;
    const filamentChanges = filaments.reduce((total, item) => total + item.settings.filter(changed).length, 0);
    const printerChanges = catalog!.printer.settings.filter(changed).length;
    const summary = (count: number) => count > 0 ? `${count} changed` : 'from the preset';
    return <>
      <div className="all-settings-presets" data-testid="all-settings-presets">
        <div className="all-settings-preset"><span className="all-settings-preset-label">Printer</span>
          <span className="all-settings-preset-value">{[identity?.printer || catalog!.printer.name,
            identity?.nozzles.length ? `${identity.nozzles.join(' / ')} mm nozzle` : ''].filter(Boolean).join(' · ')}</span></div>
        <div className="all-settings-preset"><span className="all-settings-preset-label">Filament</span>
          <span className="all-settings-preset-value all-settings-preset-value--list">
            {filaments.map((filament) => <span className="all-settings-preset-slot" key={filament.slot}>
              <Swatch colour={filament.colour} label={filament.material || shortPreset(filament.preset)} />
              Slot {filament.slot} · {shortPreset(filament.preset)}</span>)}
            {filaments.length === 0 && 'None'}</span></div>
        <div className="all-settings-preset"><span className="all-settings-preset-label">Process</span>
          <span className="all-settings-preset-value">{shortPreset(catalog!.process.name)}
            <span className="all-settings-preset-detail">{processChanges > 0
              ? `${plural(processChanges, 'change', 'changes')} from the preset` : 'from the preset'}</span></span></div>
        {activePlate && (activePlate.ownBedType || activePlate.own.length > 0) && <div className="all-settings-preset">
          <span className="all-settings-preset-label">{activePlate.name}</span>
          <span className="all-settings-preset-value">{activePlate.ownBedType ? activePlate.bedType : activePlate.own[0].display}
            <span className="all-settings-preset-detail">{activePlate.ownBedType
              ? `Bed type set on this plate${activePlate.own.length > 1 ? ` · ${plural(activePlate.own.length - 1, 'more plate setting', 'more plate settings')}` : ''}`
              : plural(activePlate.own.length, 'setting set on this plate', 'settings set on this plate')}</span></span></div>}
      </div>
      {withOwn.length > 0 && !saved && <div className="all-settings-own" role="note">
        <p>{withOwn.length === 1
          ? `${withOwn[0].name} has settings of its own${regionsOf(withOwn[0])}.`
          : `${withOwn.map((object) => object.name).join(', ')} have settings of their own.`}</p>
        <p className="all-settings-own-hint">{withOwn.length === 1 ? 'Open it' : 'Open one'} in the 3D view, or choose it above, to see them.</p>
      </div>}
      <div className="all-settings-groups">
        {PROCESS_PAGES.map((page, index) => {
          const rows = byPage.get(page) ?? [];
          const changes = catalog!.process.settings.filter((setting) => pageOf(setting) === page && changed(setting)).length;
          if (rows.length === 0 && (query.trim() || filter === 'changed')) return null;
          return <Group key={page} name={page} summary={summary(changes)} count={rows.length}
            open={isOpen(page, index === 0 || changes > 0)} onToggle={() => toggle(page, index === 0 || changes > 0)}>
            <PresetRows settings={rows} notes={overrideNotes} />
          </Group>;
        })}
        {(filamentCount > 0 || !(query.trim() || filter === 'changed')) && <Group name="Filament" count={filamentCount}
          summary={filaments.length > 1 ? `${plural(filaments.length, 'slot', 'slots')}${filaments.length <= COLUMN_SLOTS ? ', side by side' : ''}`
            : filaments[0] ? [filaments[0].material || shortPreset(filaments[0].preset), filamentChanges > 0 ? `${filamentChanges} changed` : ''].filter(Boolean).join(' · ')
            : 'none'}
          open={isOpen('Filament', filamentChanges > 0)} onToggle={() => toggle('Filament', filamentChanges > 0)}>
          {filaments.length > COLUMN_SLOTS && <div className="all-settings-slot-picker">
            <span className="all-settings-scope-label">Slot</span>
            <ActionMenu label="Slot" appearance="select" text={`Slot ${slot} · ${shortPreset(filaments.find((item) => item.slot === slot)?.preset ?? '')}`}
              items={filaments.map((item) => ({ key: String(item.slot), label: `Slot ${item.slot} · ${item.material || shortPreset(item.preset)}`,
                current: item.slot === slot, onSelect: () => setSlot(item.slot) }))} />
          </div>}
          {filamentShown.length === 1 && <PresetRows settings={filamentRowsShown} byPage />}
          {filamentShown.length > 1 && <FilamentColumns filaments={filamentShown} plate={activePlate} query={query} filter={filter} />}
        </Group>}
        {(printerRows.length > 0 || !(query.trim() || filter === 'changed')) && <Group name="Printer" count={printerRows.length}
          summary={[shortPreset(catalog!.printer.name), printerChanges > 0 ? `${printerChanges} changed` : ''].filter(Boolean).join(' · ')}
          open={isOpen('Printer', printerChanges > 0)} onToggle={() => toggle('Printer', printerChanges > 0)}>
          <PresetRows settings={printerRows} byPage />
        </Group>}
      </div>
    </>;
  };

  // "in 4 regions: 2 mounting tabs and 2 screw holes" -- the volumes an
  // object prints differently, counted by name.
  const regionsOf = (object: CatalogObjectInfo): string => {
    const names = runs(object.overrides.filter((local) => volumeName(local)), (local) => volumeName(local)!)
      .map(([name, locals]) => locals.length > 1 ? `${locals.length} ${name}` : name);
    const regions = object.overrides.filter((local) => volumeName(local)).length;
    return regions === 0 ? '' : ` in ${plural(regions, 'region', 'regions')}: ${names.join(' and ')}`;
  };

  const objectBody = (object: CatalogObjectInfo) => {
    type Entry = { key: string; label: string; value: string; tag: string; note: string; page: string; action?: RowAction };
    // The link the host's table offers under a row, sent as the row's first
    // customization names it: every one the row stands for matches it.
    const press = (local: CatalogOverrideInfo, label: string): RowAction | undefined => {
      if (!local.action || !onAction || saved) return undefined;
      const action = local.action;
      const send = () => onAction({ sessionId: context.sessionId, objectId: object.id, action, kind: local.kind,
        target: action === 'remove' ? '' : local.target, key: local.key, value: local.value });
      return action === 'remove'
        ? { text: 'Remove', name: `Remove ${label.charAt(0).toLowerCase()}${label.slice(1)}`, onPress: send }
        : { text: 'Use project value', name: `Use project value for ${label}`, onPress: send };
    };
    const processRow = (key: string) => catalog!.process.settings.find((setting) => setting.key === key);
    const tagOf = (kind: string) => kind === 'object' ? 'Object' : kind === 'height range' ? 'Height range' : 'Region';
    const entries: Entry[] = [];
    // Two modifiers of one name that set one setting the same way are one
    // row: "Mounting tabs · 2 regions".
    const valued = runs(object.overrides.filter((item) => item.key),
      (item) => [item.kind, item.key, item.value, volumeName(item) ?? ''].join('\u0000'));
    for (const [, locals] of valued) {
      const local = locals[0];
      const row = processRow(local.key);
      const where = volumeName(local) ?? (local.kind === 'object' ? 'Whole object' : sentence(local.kind));
      const regions = locals.length > 1 ? ` · ${plural(locals.length, 'region', 'regions')}` : '';
      // OrcaSlicer stores an object's filament as a slot number, the one
      // setting the panel knows by its key: the slot's preset says more.
      const slotNumber = local.key === 'extruder' ? Number(local.value) : NaN;
      const filament = Number.isInteger(slotNumber) ? catalog!.filaments.find((item) => item.slot === slotNumber) : undefined;
      const projectValue = local.key === 'extruder'
        ? catalog!.filaments[0] ? `1 · ${shortPreset(catalog!.filaments[0].preset)}` : '1' : row?.display ?? 'preset';
      const label = local.label || sentence(local.key);
      entries.push({ key: `${local.target}:${local.kind}:${local.key}:${local.value}`, label,
        value: filament ? `${slotNumber} · ${shortPreset(filament.preset)}` : local.display || local.value, tag: tagOf(local.kind),
        note: `${where}${regions} · project: ${projectValue}`,
        page: local.key === 'extruder' ? 'Filament' : row ? pageOf(row) : 'Others', action: press(local, label) });
    }
    for (const [kind, locals] of runs(object.overrides.filter((item) => !item.key), (item) => item.kind)) {
      const names = locals.map((local) => volumeName(local) ?? object.name);
      const label = locals.length > 1 && /blocker|enforcer|part$/.test(kind) ? `${sentence(kind)}s` : sentence(kind);
      entries.push({ key: kind, label,
        value: 'On', tag: /blocker|enforcer|part$/.test(kind) ? 'Region' : 'Object',
        note: `${names.join(', ')}${locals.length > 1 ? ` · ${plural(locals.length, 'region', 'regions')}` : ''}`,
        page: kind.includes('support') ? 'Support' : 'Others', action: press(locals[0], label) });
    }
    const shown = entries.filter((entry) => matches(entry, query));
    const byPage = new Map(runs(shown, (entry) => entry.page));
    const pages = [...PROCESS_PAGES, 'Filament', 'Printer'];
    return <>
      <div className="all-settings-presets" role="note">
        <p className="all-settings-object-summary">{object.overrides.length === 0
          ? `${object.name} uses the project’s settings and sets nothing of its own.`
          : `${object.name} uses the project’s settings, except${regionsOf(object) || ` for ${plural(object.overrides.length, 'setting', 'settings')} of its own`}.`}</p>
      </div>
      <div className="all-settings-groups">
        {pages.map((page) => {
          const rows = byPage.get(page) ?? [];
          if (rows.length === 0 && query.trim()) return null;
          return <Group key={page} name={page} count={rows.length} summary={rows.length > 0 ? plural(rows.length, 'own setting', 'own settings') : 'same as project'}
            open={isOpen(`object:${page}`, true)} onToggle={() => toggle(`object:${page}`, true)}>
            {rows.map((entry) => <Row key={entry.key} label={entry.label} value={entry.value} tag={entry.tag} notes={[entry.note]}
              action={entry.action} />)}
          </Group>;
        })}
      </div>
    </>;
  };

  const plateBody = (plate: CatalogPlateInfo) => {
    const rows = plate.own.filter((setting) => matches(setting, query));
    const here = objects.filter((object) => object.plateId === plate.id);
    return <>
      <div className="all-settings-presets" role="note">
        <p className="all-settings-object-summary">{plate.own.length === 0
          ? `${plate.name} prints with the project’s settings and sets nothing of its own.`
          : `${plate.name} sets ${plural(plate.own.length, 'setting', 'settings')} of its own; the rest is the project’s.`}</p>
      </div>
      <div className="all-settings-groups">
        <Group name="Plate" count={rows.length} summary={rows.length > 0 ? plural(rows.length, 'own setting', 'own settings') : 'same as project'}
          open={isOpen('plate', true)} onToggle={() => toggle('plate', true)}>
          {rows.map((setting) => <Row key={setting.key} label={setting.label} value={setting.display} bold notes={['Set on this plate']} />)}
        </Group>
        <Group name="Objects" count={here.length} summary={plural(here.length, 'object', 'objects')}
          open={isOpen('plate-objects', true)} onToggle={() => toggle('plate-objects', true)}>
          {here.map((object) => <div className="all-settings-row" key={object.id}>
            <div className="all-settings-row-line">
              <button type="button" className="all-settings-object-link" onClick={() => setChosen({ kind: 'object', id: object.id })}>{object.name}</button>
              <span className="all-settings-row-value all-settings-row-value--muted">{object.overrides.length > 0
                ? plural(object.overrides.length, 'own setting', 'own settings') : 'same as project'}</span>
            </div>
          </div>)}
        </Group>
      </div>
    </>;
  };

  return <main className="all-settings" data-testid="all-settings" data-scope={scopeKey(scope)}>
    <div className="all-settings-scope">
      <span className="all-settings-scope-label" id="all-settings-showing">Showing</span>
      <ActionMenu label="Showing" appearance="select" text={shownName} items={options} />
    </div>
    <p className="all-settings-hint">{saved ? 'A saved chat’s setup: the settings that differed from the presets when it was saved.'
      : scope.kind === 'object' ? `Follows the selection in the 3D view. Shown: only what ${shownName} sets itself.`
      : scope.kind === 'plate' ? `Follows the selection in the 3D view. Shown: what ${shownName} sets itself, and its objects.`
      : 'Follows the selection in the 3D view.'}</p>
    <div className="all-settings-find">
      <label className="field-search all-settings-search">
        <span className="jp-icon jp-icon-search" aria-hidden="true" />
        <input type="search" placeholder="Search settings" aria-label="Search settings" value={query}
          onChange={(event) => setQuery(event.target.value)} />
      </label>
      <div className="jp-segmented all-settings-filter" role="radiogroup" aria-label="Which settings">
        {(['all', 'changed'] as const).map((choice) => <button type="button" key={choice} role="radio"
          aria-checked={filter === choice} disabled={saved && choice === 'all'}
          className={filter === choice ? 'jp-segment selected' : 'jp-segment'} onClick={() => setFilter(choice)}>
          {choice === 'all' ? 'All' : 'Changed'}</button>)}
      </div>
    </div>
    {body()}
  </main>;
}
