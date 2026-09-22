import { useEffect, useRef, useState } from 'react';
import type { PrinterInfo } from '../bridge/protocol';
import { MoreGlyph } from './Glyphs';

export interface PrinterActions {
  onConnect?: (id: string) => void;
  onOpenSettings: (id: string) => void;
  // A device is renamed in Orca's own dialog, so it is sent without a name.
  onRename: (id: string, name?: string) => void;
  onRemove: (id: string) => void;
}

// The characters Orca refuses in a profile name (SavePresetDialog). The host
// checks again, against names only it knows.
const ILLEGAL = /[<>[\]:/\\|?*"]/;

export function renameProblem(name: string, current: string, others: string[]): string | undefined {
  const trimmed = name.trim();
  const folded = trimmed.toLowerCase();
  if (!trimmed) return 'Enter a name for the printer.';
  if (ILLEGAL.test(trimmed)) return 'A printer name can’t contain any of these characters: < > [ ] : / \\ | ? * "';
  if (trimmed !== current && folded === current.toLowerCase())
    return 'A new name has to differ from the old one by more than capitalization.';
  if (others.some((other) => other.toLowerCase() === folded)) return `Another printer is already named “${trimmed}”.`;
  return undefined;
}

// The card header's kebab: Printer settings…, Rename, Remove printer…. A named
// printer's rename and removal are confirmed here; a device's open Orca's own
// dialogs, which confirm for themselves.
export function PrinterMenu({
  printer,
  otherNames,
  actions,
}: {
  printer: PrinterInfo;
  otherNames: string[];
  actions: PrinterActions;
}) {
  const [open, setOpen] = useState(false);
  const [dialog, setDialog] = useState<'rename' | 'remove' | null>(null);
  const [name, setName] = useState(printer.name);
  const root = useRef<HTMLDivElement>(null);
  const button = useRef<HTMLButtonElement>(null);
  const panel = useRef<HTMLDivElement>(null);
  const input = useRef<HTMLInputElement>(null);

  useEffect(() => {
    if (!open) return;
    root.current?.querySelector<HTMLButtonElement>('[role="menuitem"]:not(:disabled)')?.focus();
    const dismiss = (event: PointerEvent) => {
      if (!root.current?.contains(event.target as Node)) setOpen(false);
    };
    document.addEventListener('pointerdown', dismiss);
    return () => document.removeEventListener('pointerdown', dismiss);
  }, [open]);

  useEffect(() => {
    if (dialog === 'rename') {
      input.current?.focus();
      input.current?.select();
    }
    if (dialog === 'remove') panel.current?.querySelector<HTMLButtonElement>('button')?.focus();
  }, [dialog]);

  const closeDialog = () => {
    setDialog(null);
    button.current?.focus();
  };
  const named = printer.kind === 'named';
  const problem = renameProblem(name, printer.name, otherNames);

  const rename = () => {
    setOpen(false);
    if (!named) {
      actions.onRename(printer.id);
      return;
    }
    setName(printer.name);
    setDialog('rename');
  };
  const remove = () => {
    setOpen(false);
    if (named) setDialog('remove');
    else actions.onRemove(printer.id);
  };

  return (
    <>
      <div
        className="printer-actions"
        ref={root}
        onKeyDown={(event) => {
          if (event.key === 'Escape') {
            setOpen(false);
            button.current?.focus();
          }
          if (open && ['ArrowDown', 'ArrowUp', 'Home', 'End'].includes(event.key)) {
            event.preventDefault();
            const items = Array.from(
              root.current!.querySelectorAll<HTMLButtonElement>('[role="menuitem"]:not(:disabled)'),
            );
            const index = items.indexOf(document.activeElement as HTMLButtonElement);
            const next =
              event.key === 'Home'
                ? 0
                : event.key === 'End'
                  ? items.length - 1
                  : (index + (event.key === 'ArrowUp' ? -1 : 1) + items.length) % items.length;
            items[next]?.focus();
          }
        }}
        onBlur={(event) => {
          // WebKit does not move focus to a <button> on mouse click (only on
          // keyboard activation), so clicking one menu item after another
          // fires blur with relatedTarget: null here -- not because focus
          // left the menu, but because WebKit never granted it to begin
          // with. Treating that as "left" closed the menu before the
          // item's own click could land. A real relatedTarget (Tab moving
          // focus) still closes the menu when it points outside; pointer
          // clicks outside are already handled by the dismiss listener
          // below, which checks the actual click target instead of focus.
          if (event.relatedTarget && !event.currentTarget.contains(event.relatedTarget)) setOpen(false);
        }}
      >
        <button
          ref={button}
          type="button"
          className="printer-menu-button"
          aria-label={`Actions for ${printer.name}`}
          title="Printer actions"
          aria-haspopup="menu"
          aria-expanded={open}
          onClick={() => setOpen(!open)}
        >
          <MoreGlyph />
        </button>
        {open && (
          <div className="printer-menu" role="menu" aria-label={`Actions for ${printer.name}`}>
            {named && actions.onConnect && <button type="button" role="menuitem" onClick={() => {
              setOpen(false);
              actions.onConnect?.(printer.id);
            }}>Connect printer…</button>}
            <button
              type="button"
              role="menuitem"
              disabled={!printer.canOpenSettings}
              onClick={() => {
                setOpen(false);
                actions.onOpenSettings(printer.id);
              }}
            >
              Printer settings…
            </button>
            <button type="button" role="menuitem" disabled={!printer.canRename} onClick={rename}>
              Rename
            </button>
            <button type="button" role="menuitem" className="danger" disabled={!printer.canRemove} onClick={remove}>
              Remove printer…
            </button>
          </div>
        )}
      </div>
      {dialog && (
        <div className="printer-dialog-shade">
          <div
            ref={panel}
            className="printer-dialog"
            role="dialog"
            aria-modal="true"
            aria-labelledby={`printer-dialog-title-${printer.id}`}
            onKeyDown={(event) => {
              if (event.key === 'Escape') {
                event.stopPropagation();
                closeDialog();
              }
              if (event.key === 'Tab') {
                const controls = Array.from(panel.current!.querySelectorAll<HTMLElement>('input, button:not(:disabled)'));
                const first = controls[0];
                const last = controls[controls.length - 1];
                if (event.shiftKey && document.activeElement === first) {
                  event.preventDefault();
                  last.focus();
                }
                if (!event.shiftKey && document.activeElement === last) {
                  event.preventDefault();
                  first.focus();
                }
              }
            }}
          >
            <h2 id={`printer-dialog-title-${printer.id}`}>
              {dialog === 'rename' ? 'Rename printer' : 'Remove printer?'}
            </h2>
            {dialog === 'rename' ? (
              <form
                onSubmit={(event) => {
                  event.preventDefault();
                  if (problem) return;
                  const trimmed = name.trim();
                  if (trimmed !== printer.name) actions.onRename(printer.id, trimmed);
                  closeDialog();
                }}
              >
                <label htmlFor={`printer-name-${printer.id}`}>Printer name</label>
                <input
                  id={`printer-name-${printer.id}`}
                  ref={input}
                  value={name}
                  onChange={(event) => setName(event.target.value)}
                />
                {problem && <p role="alert">{problem}</p>}
                <div className="printer-dialog-buttons">
                  <button type="button" className="button-secondary" onClick={closeDialog}>
                    Cancel
                  </button>
                  <button type="submit" className="button-primary" disabled={problem !== undefined}>
                    Save
                  </button>
                </div>
              </form>
            ) : (
              <>
                <p>
                  Remove “{printer.name}”? Its printer settings and remembered spools will be deleted. Your projects
                  stay.
                </p>
                <div className="printer-dialog-buttons">
                  <button type="button" className="button-secondary" onClick={closeDialog}>
                    Cancel
                  </button>
                  <button
                    type="button"
                    className="button-danger"
                    onClick={() => {
                      actions.onRemove(printer.id);
                      closeDialog();
                    }}
                  >
                    Remove
                  </button>
                </div>
              </>
            )}
          </div>
        </div>
      )}
    </>
  );
}
