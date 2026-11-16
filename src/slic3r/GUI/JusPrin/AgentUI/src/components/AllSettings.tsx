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
import { i18n } from '@lingui/core';
import { t, plural } from '@lingui/core/macro';
import { CatalogFilamentInfo, CatalogObjectInfo, CatalogOverrideInfo, CatalogPlateInfo, CatalogSettingInfo,
  SettingsActionPayload, SettingsCatalogPayload, WorkspaceContext } from '../bridge/protocol';
import { ActionMenu, AgentPaneToggle, MenuItem } from './ChatNavigation';

export type SettingsScope = { kind: 'project' } | { kind: 'plate'; id: string } | { kind: 'object'; id: string };
export type SettingsFilter = 'all' | 'changed';

// OrcaSlicer's process tab pages, in its order. A setting the tab does not
// index (empty page) is filed under Others. These are the host's names for
// the pages, which it sends untranslated; pageName says them.
export const PROCESS_PAGES = ['Quality', 'Strength', 'Speed', 'Support', 'Multimaterial', 'Others'];

// A group's heading: a process page, or one of the panel's own groups.
function pageName(page: string): string {
  switch (page) {
    case 'Quality': return t`Quality`;
    case 'Strength': return t`Strength`;
    case 'Speed': return t`Speed`;
    case 'Support': return t({ message: 'Support', comment: 'The settings page for support structures' });
    case 'Multimaterial': return t`Multimaterial`;
    case 'Others': return t`Others`;
    case 'Filament': return t`Filament`;
    case 'Printer': return t`Printer`;
    default: return page;
  }
}
// A setting's page as the person reads it: the panel's own words for its
// pages, else the host's translation.
const shownPage = (setting: CatalogSettingInfo) =>
  PROCESS_PAGES.includes(setting.page) ? pageName(setting.page) : setting.pageLabel || setting.page;

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

function shortPreset(name: string): string {
  const separator = name.indexOf(' @');
  return separator < 0 ? name : name.slice(0, separator);
}

const sentence = (text: string) => {
  const words = text.replace(/[_-]+/g, ' ').trim();
  return words.charAt(0).toUpperCase() + words.slice(1);
};

// The name of a kind of customization (CatalogOverrideInfo.kind), as a row
// says it; `count` names a run of volumes of one kind. A kind the host adds
// later reads as its own words.
function kindName(kind: string, count = 1): string {
  switch (kind) {
    case 'object': return t`Object`;
    case 'modifier': return t`Modifier`;
    case 'part': return plural(count, { one: 'Part', other: 'Parts' });
    case 'negative part': return plural(count, { one: 'Negative part', other: 'Negative parts' });
    case 'support blocker': return plural(count, { one: 'Support blocker', other: 'Support blockers' });
    case 'support enforcer': return plural(count, { one: 'Support enforcer', other: 'Support enforcers' });
    case 'height range': return t`Height range`;
    case 'variable layer height': return t`Variable layer height`;
    case 'support painting': return t`Support painting`;
    case 'seam painting': return t`Seam painting`;
    case 'color painting': return t`Color painting`;
    case 'fuzzy skin painting': return t`Fuzzy skin painting`;
    default: return count > 1 && /blocker|enforcer|part$/.test(kind) ? `${sentence(kind)}s` : sentence(kind);
  }
}

// Names joined as a sentence lists them: "2 mounting tabs and 2 screw holes".
function joinNames(names: string[]): string {
  return names.reduce((list, name) => t`${list} and ${name}`);
}

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

// A group's count of settings changed from the preset: "3 changed".
const changedCount = (count: number) => plural(count, { one: '# changed', other: '# changed' });

// A group's or an object's count of settings of its own.
const ownSettings = (count: number) => count > 0
  ? plural(count, { one: '# own setting', other: '# own settings' }) : t`same as project`;

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

// What a row says for a value the filament leaves to the printer.
const inheritedValue = () => t({ message: 'printer', comment: 'A filament setting’s value: the printer’s value is used' });

function presetNote(setting: CatalogSettingInfo): string {
  const preset = setting.presetDisplay ?? setting.preset ?? '';
  return t`Preset: ${preset}`;
}

function inheritedNote(setting: CatalogSettingInfo): string {
  const value = setting.inheritedDisplay ?? '';
  return t`From the printer: ${value}`;
}

// A filament slot and its preset, as the panel names one.
function slotName(slot: number, preset: string): string {
  return t`Slot ${slot} · ${preset}`;
}

// Rows of one preset under their Orca groups, as a name and a value; a
// changed value is bold with the preset's under it.
function PresetRows({ settings, notes, byPage }: {
  settings: CatalogSettingInfo[]; notes?: (setting: CatalogSettingInfo) => string[]; byPage?: boolean;
}) {
  return <>{runs(settings, (setting) => byPage ? [shownPage(setting), setting.group].filter(Boolean).join(' · ') : setting.group)
    .map(([heading, rows]) => <div className="all-settings-run" key={heading || '(ungrouped)'}>
      {heading && <p className="all-settings-run-name">{heading}</p>}
      {rows.map((setting) => <Row key={setting.key} label={setting.label} bold={changed(setting)}
        muted={setting.inherited} value={setting.inherited ? inheritedValue() : setting.display}
        notes={[changed(setting) ? presetNote(setting) : '',
          setting.inherited ? inheritedNote(setting) : '', ...(notes?.(setting) ?? [])]} />)}
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
        <Swatch colour={filament.colour} label={slotName(filament.slot, shortPreset(filament.preset))} />
        <span className="all-settings-column-slot-name">{slotLabel(filament.slot)}</span>
        <span className="all-settings-column-slot-material">{filament.material || shortPreset(filament.preset)}</span>
      </span>)}
    </div>
    {runs(shown, (setting) => [shownPage(setting), setting.group].filter(Boolean).join(' · ')).map(([heading, group]) =>
      <div className="all-settings-run" key={heading || '(ungrouped)'}>
        {heading && <p className="all-settings-run-name">{heading}</p>}
        {group.map((lead) => {
          const cells = perSlot.map((slot) => slot.get(lead.key));
          const notes = cells.flatMap((cell, index) => {
            if (!cell) return [];
            const slot = filaments[index].slot;
            const lines: string[] = [];
            const preset = cell.presetDisplay ?? cell.preset ?? '';
            const value = printerValue(perSlot, lead.key);
            if (changed(cell)) lines.push(t`Slot ${slot} preset: ${preset}`);
            if (overridesPrinter(cell, perSlot, lead.key))
              lines.push(t`Slot ${slot} overrides the printer’s ${value}.`);
            return lines;
          });
          const anyBold = cells.some((cell) => cell && (changed(cell) || overridesPrinter(cell, perSlot, lead.key)));
          return <div className={anyBold ? 'all-settings-row all-settings-row--changed' : 'all-settings-row'} key={lead.key}>
            <div className="all-settings-row-line all-settings-row-line--columns">
              <span className="all-settings-row-name" title={lead.label}>{lead.label}</span>
              {cells.map((cell, index) => <span key={index} className={['all-settings-row-value',
                cell && (changed(cell) || overridesPrinter(cell, perSlot, lead.key)) && 'all-settings-row-value--bold',
                cell?.inherited && 'all-settings-row-value--muted'].filter(Boolean).join(' ')}>
                {cell ? cell.inherited ? inheritedValue() : cell.display : '—'}</span>)}
            </div>
            {notes.map((note) => <p className="all-settings-row-note" key={note}>{note}</p>)}
          </div>;
        })}
      </div>)}
  </div>;
}

function slotLabel(slot: number): string {
  return t`Slot ${slot}`;
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
    <button type="button" className="chat-icon chat-back" aria-label={t`Back to chat`} title={t`Back to chat`} autoFocus onClick={onBack}>
      <span className="jp-icon jp-icon-chevron-left" aria-hidden="true" />
    </button>
    <h1>{t`All settings`}</h1>
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
    { key: 'project', label: t`Whole project`, current: scope.kind === 'project', onSelect: () => setChosen({ kind: 'project' }) },
    ...(plates.length > 1 ? plates.map((plate) => ({ key: `plate:${plate.id}`, label: plate.name,
      current: scope.kind === 'plate' && scope.id === plate.id, onSelect: () => setChosen({ kind: 'plate', id: plate.id }) })) : []),
    ...objects.map((object) => ({ key: `object:${object.id}`, label: object.name,
      current: scope.kind === 'object' && scope.id === object.id, onSelect: () => setChosen({ kind: 'object', id: object.id }) })),
  ];
  const shownName = options.find((option) => option.current)?.label ?? t`Whole project`;

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
    if (unsupported) return <p className="all-settings-note">{t`This JusPrin build cannot list the settings.`}</p>;
    if (!catalog) return <p className="all-settings-note" role="status">{t`Reading the settings…`}</p>;
    if (!catalog.available) return <p className="all-settings-note">{t`No settings to show: the selected printer is not an FFF printer.`}</p>;
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
    const summary = (count: number) => count > 0 ? changedCount(count) : t`from the preset`;
    const nozzles = identity?.nozzles.length ? identity.nozzles.join(' / ') : '';
    const plateOwn = activePlate?.own.length ?? 0;
    const morePlate = plateOwn - 1;
    const slots = filaments.length;
    return <>
      <div className="all-settings-presets" data-testid="all-settings-presets">
        <div className="all-settings-preset"><span className="all-settings-preset-label">{t`Printer`}</span>
          <span className="all-settings-preset-value">{[identity?.printer || catalog!.printer.name,
            nozzles ? t`${nozzles} mm nozzle` : ''].filter(Boolean).join(' · ')}</span></div>
        <div className="all-settings-preset"><span className="all-settings-preset-label">{t`Filament`}</span>
          <span className="all-settings-preset-value all-settings-preset-value--list">
            {filaments.map((filament) => <span className="all-settings-preset-slot" key={filament.slot}>
              <Swatch colour={filament.colour} label={filament.material || shortPreset(filament.preset)} />
              {slotName(filament.slot, shortPreset(filament.preset))}</span>)}
            {filaments.length === 0 && t`None`}</span></div>
        <div className="all-settings-preset"><span className="all-settings-preset-label">{t`Process`}</span>
          <span className="all-settings-preset-value">{shortPreset(catalog!.process.name)}
            <span className="all-settings-preset-detail">{processChanges > 0
              ? plural(processChanges, { one: '# change from the preset', other: '# changes from the preset' })
              : t`from the preset`}</span></span></div>
        {activePlate && (activePlate.ownBedType || activePlate.own.length > 0) && <div className="all-settings-preset">
          <span className="all-settings-preset-label">{activePlate.name}</span>
          <span className="all-settings-preset-value">{activePlate.ownBedType ? activePlate.bedType : activePlate.own[0].display}
            <span className="all-settings-preset-detail">{activePlate.ownBedType
              ? morePlate > 0
                ? plural(morePlate, { one: 'Bed type set on this plate · # more plate setting', other: 'Bed type set on this plate · # more plate settings' })
                : t`Bed type set on this plate`
              : plural(plateOwn, { one: '# setting set on this plate', other: '# settings set on this plate' })}</span></span></div>}
      </div>
      {withOwn.length > 0 && !saved && <div className="all-settings-own" role="note">
        <p>{withOwn.length === 1 ? hasOwn(withOwn[0]) : manyHaveOwn(withOwn)}</p>
        <p className="all-settings-own-hint">{withOwn.length === 1
          ? t`Open it in the 3D view, or choose it above, to see them.`
          : t`Open one in the 3D view, or choose it above, to see them.`}</p>
      </div>}
      <div className="all-settings-groups">
        {PROCESS_PAGES.map((page, index) => {
          const rows = byPage.get(page) ?? [];
          const changes = catalog!.process.settings.filter((setting) => pageOf(setting) === page && changed(setting)).length;
          if (rows.length === 0 && (query.trim() || filter === 'changed')) return null;
          return <Group key={page} name={pageName(page)} summary={summary(changes)} count={rows.length}
            open={isOpen(page, index === 0 || changes > 0)} onToggle={() => toggle(page, index === 0 || changes > 0)}>
            <PresetRows settings={rows} notes={overrideNotes} />
          </Group>;
        })}
        {(filamentCount > 0 || !(query.trim() || filter === 'changed')) && <Group name={pageName('Filament')} count={filamentCount}
          summary={slots > 1 ? slots <= COLUMN_SLOTS
            ? plural(slots, { one: '# slot, side by side', other: '# slots, side by side' })
            : plural(slots, { one: '# slot', other: '# slots' })
            : filaments[0] ? [filaments[0].material || shortPreset(filaments[0].preset), filamentChanges > 0 ? changedCount(filamentChanges) : ''].filter(Boolean).join(' · ')
            : t`none`}
          open={isOpen('Filament', filamentChanges > 0)} onToggle={() => toggle('Filament', filamentChanges > 0)}>
          {filaments.length > COLUMN_SLOTS && <div className="all-settings-slot-picker">
            <span className="all-settings-scope-label">{t`Slot`}</span>
            <ActionMenu label={t`Slot`} appearance="select" text={slotName(slot, shortPreset(filaments.find((item) => item.slot === slot)?.preset ?? ''))}
              items={filaments.map((item) => ({ key: String(item.slot), label: slotName(item.slot, item.material || shortPreset(item.preset)),
                current: item.slot === slot, onSelect: () => setSlot(item.slot) }))} />
          </div>}
          {filamentShown.length === 1 && <PresetRows settings={filamentRowsShown} byPage />}
          {filamentShown.length > 1 && <FilamentColumns filaments={filamentShown} plate={activePlate} query={query} filter={filter} />}
        </Group>}
        {(printerRows.length > 0 || !(query.trim() || filter === 'changed')) && <Group name={pageName('Printer')} count={printerRows.length}
          summary={[shortPreset(catalog!.printer.name), printerChanges > 0 ? changedCount(printerChanges) : ''].filter(Boolean).join(' · ')}
          open={isOpen('Printer', printerChanges > 0)} onToggle={() => toggle('Printer', printerChanges > 0)}>
          <PresetRows settings={printerRows} byPage />
        </Group>}
      </div>
    </>;
  };

  // "4 regions: 2 mounting tabs and 2 screw holes" -- the volumes an object
  // prints differently, counted by name.
  const regionsOf = (object: CatalogObjectInfo): { regions: number; names: string } => {
    const names = runs(object.overrides.filter((local) => volumeName(local)), (local) => volumeName(local)!)
      .map(([name, locals]) => locals.length > 1 ? `${locals.length} ${name}` : name);
    const regions = object.overrides.filter((local) => volumeName(local)).length;
    return { regions, names: regions === 0 ? '' : joinNames(names) };
  };

  // The project view's note on objects with settings of their own.
  const hasOwn = (object: CatalogObjectInfo): string => {
    const name = object.name;
    const { regions, names } = regionsOf(object);
    return regions === 0 ? t`${name} has settings of its own.` : plural(regions, {
      one: `${name} has settings of its own in # region: ${names}.`,
      other: `${name} has settings of its own in # regions: ${names}.`,
    });
  };
  const manyHaveOwn = (objects: CatalogObjectInfo[]): string => {
    const names = objects.map((object) => object.name).join(', ');
    return t`${names} have settings of their own.`;
  };

  const objectSummary = (object: CatalogObjectInfo): string => {
    const name = object.name;
    const own = object.overrides.length;
    const { regions, names } = regionsOf(object);
    if (own === 0) return t`${name} uses the project’s settings and sets nothing of its own.`;
    if (regions > 0) return plural(regions, {
      one: `${name} uses the project’s settings, except in # region: ${names}.`,
      other: `${name} uses the project’s settings, except in # regions: ${names}.`,
    });
    return plural(own, {
      one: `${name} uses the project’s settings, except for # setting of its own.`,
      other: `${name} uses the project’s settings, except for # settings of its own.`,
    });
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
      // English lowers the name inside the sentence ("Remove layer height");
      // other languages keep it as written (German capitalizes its nouns).
      const lowered = i18n.locale === 'en' ? `${label.charAt(0).toLowerCase()}${label.slice(1)}` : label;
      return action === 'remove'
        ? { text: t`Remove`, name: t`Remove ${lowered}`, onPress: send }
        : { text: t`Use project value`, name: t`Use project value for ${label}`, onPress: send };
    };
    const processRow = (key: string) => catalog!.process.settings.find((setting) => setting.key === key);
    const tagOf = (kind: string) => kind === 'object' ? t`Object` : kind === 'height range' ? t`Height range` : t`Region`;
    const entries: Entry[] = [];
    // Two modifiers of one name that set one setting the same way are one
    // row: "Mounting tabs · 2 regions".
    const valued = runs(object.overrides.filter((item) => item.key),
      (item) => [item.kind, item.key, item.value, volumeName(item) ?? ''].join('\u0000'));
    for (const [, locals] of valued) {
      const local = locals[0];
      const row = processRow(local.key);
      const where = volumeName(local) ?? (local.kind === 'object' ? t`Whole object` : kindName(local.kind));
      const regions = locals.length;
      // OrcaSlicer stores an object's filament as a slot number, the one
      // setting the panel knows by its key: the slot's preset says more.
      const slotNumber = local.key === 'extruder' ? Number(local.value) : NaN;
      const filament = Number.isInteger(slotNumber) ? catalog!.filaments.find((item) => item.slot === slotNumber) : undefined;
      const projectValue = local.key === 'extruder'
        ? catalog!.filaments[0] ? `1 · ${shortPreset(catalog!.filaments[0].preset)}` : '1'
        : row?.display ?? t({ message: 'preset', comment: 'A setting’s project value: whatever the preset says' });
      const label = local.label || sentence(local.key);
      entries.push({ key: `${local.target}:${local.kind}:${local.key}:${local.value}`, label,
        value: filament ? `${slotNumber} · ${shortPreset(filament.preset)}` : local.display || local.value, tag: tagOf(local.kind),
        note: regions > 1
          ? plural(regions, { one: `${where} · # region · project: ${projectValue}`, other: `${where} · # regions · project: ${projectValue}` })
          : t`${where} · project: ${projectValue}`,
        page: local.key === 'extruder' ? 'Filament' : row ? pageOf(row) : 'Others', action: press(local, label) });
    }
    for (const [kind, locals] of runs(object.overrides.filter((item) => !item.key), (item) => item.kind)) {
      const names = locals.map((local) => volumeName(local) ?? object.name).join(', ');
      const regions = locals.length;
      const label = kindName(kind, regions);
      entries.push({ key: kind, label,
        value: t`On`, tag: /blocker|enforcer|part$/.test(kind) ? t`Region` : t`Object`,
        note: regions > 1 ? plural(regions, { one: `${names} · # region`, other: `${names} · # regions` }) : names,
        page: kind.includes('support') ? 'Support' : 'Others', action: press(locals[0], label) });
    }
    const shown = entries.filter((entry) => matches(entry, query));
    const byPage = new Map(runs(shown, (entry) => entry.page));
    const pages = [...PROCESS_PAGES, 'Filament', 'Printer'];
    return <>
      <div className="all-settings-presets" role="note">
        <p className="all-settings-object-summary">{objectSummary(object)}</p>
      </div>
      <div className="all-settings-groups">
        {pages.map((page) => {
          const rows = byPage.get(page) ?? [];
          if (rows.length === 0 && query.trim()) return null;
          return <Group key={page} name={pageName(page)} count={rows.length} summary={ownSettings(rows.length)}
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
    const name = plate.name;
    const own = plate.own.length;
    const objectCount = here.length;
    return <>
      <div className="all-settings-presets" role="note">
        <p className="all-settings-object-summary">{own === 0
          ? t`${name} prints with the project’s settings and sets nothing of its own.`
          : plural(own, {
            one: `${name} sets # setting of its own; the rest is the project’s.`,
            other: `${name} sets # settings of its own; the rest is the project’s.`,
          })}</p>
      </div>
      <div className="all-settings-groups">
        <Group name={t`Plate`} count={rows.length} summary={ownSettings(rows.length)}
          open={isOpen('plate', true)} onToggle={() => toggle('plate', true)}>
          {rows.map((setting) => <Row key={setting.key} label={setting.label} value={setting.display} bold notes={[t`Set on this plate`]} />)}
        </Group>
        <Group name={t`Objects`} count={objectCount} summary={plural(objectCount, { one: '# object', other: '# objects' })}
          open={isOpen('plate-objects', true)} onToggle={() => toggle('plate-objects', true)}>
          {here.map((object) => <div className="all-settings-row" key={object.id}>
            <div className="all-settings-row-line">
              <button type="button" className="all-settings-object-link" onClick={() => setChosen({ kind: 'object', id: object.id })}>{object.name}</button>
              <span className="all-settings-row-value all-settings-row-value--muted">{ownSettings(object.overrides.length)}</span>
            </div>
          </div>)}
        </Group>
      </div>
    </>;
  };

  return <main className="all-settings" data-testid="all-settings" data-scope={scopeKey(scope)}>
    <div className="all-settings-scope">
      <span className="all-settings-scope-label" id="all-settings-showing">{t`Showing`}</span>
      <ActionMenu label={t`Showing`} appearance="select" text={shownName} items={options} />
    </div>
    <p className="all-settings-hint">{saved ? t`A saved chat’s setup: the settings that differed from the presets when it was saved.`
      : scope.kind === 'object' ? t`Follows the selection in the 3D view. Shown: only what ${shownName} sets itself.`
      : scope.kind === 'plate' ? t`Follows the selection in the 3D view. Shown: what ${shownName} sets itself, and its objects.`
      : t`Follows the selection in the 3D view.`}</p>
    <div className="all-settings-find">
      <label className="field-search all-settings-search">
        <span className="jp-icon jp-icon-search" aria-hidden="true" />
        <input type="search" placeholder={t`Search settings`} aria-label={t`Search settings`} value={query}
          onChange={(event) => setQuery(event.target.value)} />
      </label>
      <div className="jp-segmented all-settings-filter" role="radiogroup" aria-label={t`Which settings`}>
        {(['all', 'changed'] as const).map((choice) => <button type="button" key={choice} role="radio"
          aria-checked={filter === choice} disabled={saved && choice === 'all'}
          className={filter === choice ? 'jp-segment selected' : 'jp-segment'} onClick={() => setFilter(choice)}>
          {choice === 'all' ? t`All` : t`Changed`}</button>)}
      </div>
    </div>
    {body()}
  </main>;
}
