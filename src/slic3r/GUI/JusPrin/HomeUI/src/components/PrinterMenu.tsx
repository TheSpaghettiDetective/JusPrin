import { useEffect, useRef, useState } from 'react';
import { t } from '@lingui/core/macro';
import { Trans } from '@lingui/react/macro';
import type { PrinterInfo } from '../bridge/protocol';
import { MoreGlyph } from './Glyphs';

export interface PrinterActions {
  onConnect?: (id: string) => void;
  // Orca's settings window, and the printer's chat about them.
  onOpenSettings: (id: string) => void;
  onAskAboutSettings: (id: string) => void;
  // A calibration of a filament on this saved printer, in the project chat.
  onCalibrate?: (id: string) => void;
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
  if (!trimmed) return t`Enter a name for the printer.`;
  if (ILLEGAL.test(trimmed)) return t`A printer name can’t contain any of these characters: < > [ ] : / \\ | ? * "`;
  if (trimmed !== current && folded === current.toLowerCase())
    return t`A new name has to differ from the old one by more than capitalization.`;
  if (others.some((other) => other.toLowerCase() === folded)) return t`Another printer is already named “${trimmed}”.`;
  return undefined;
}

// The card header's kebab: Printer settings…, Ask AI about printer settings…,
// Calibrate…, Rename, Remove printer…. A named
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
  const printerName = printer.name;
  const actionsLabel = t`Actions for ${printerName}`;

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
          aria-label={actionsLabel}
          title={t`Printer actions`}
          aria-haspopup="menu"
          aria-expanded={open}
          onClick={() => setOpen(!open)}
        >
          <MoreGlyph />
        </button>
        {open && (
          <div className="printer-menu" role="menu" aria-label={actionsLabel}>
            {named && actions.onConnect && <button type="button" role="menuitem" onClick={() => {
              setOpen(false);
              actions.onConnect?.(printer.id);
            }}><Trans>Connect…</Trans></button>}
            <button
              type="button"
              role="menuitem"
              disabled={!printer.canOpenSettings}
              onClick={() => {
                setOpen(false);
                actions.onOpenSettings(printer.id);
              }}
            >
              <Trans>Printer settings…</Trans>
            </button>
            <button
              type="button"
              role="menuitem"
              disabled={!printer.canOpenSettings}
              onClick={() => {
                setOpen(false);
                actions.onAskAboutSettings(printer.id);
              }}
            >
              <Trans>Ask AI about printer settings…</Trans>
            </button>
            {named && actions.onCalibrate && (
              <button
                type="button"
                role="menuitem"
                onClick={() => {
                  setOpen(false);
                  actions.onCalibrate?.(printer.id);
                }}
              >
                <Trans>Calibrate…</Trans>
              </button>
            )}
            <button type="button" role="menuitem" disabled={!printer.canRename} onClick={rename}>
              <Trans>Rename</Trans>
            </button>
            <button type="button" role="menuitem" className="danger" disabled={!printer.canRemove} onClick={remove}>
              <Trans>Remove printer…</Trans>
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
              {dialog === 'rename' ? t`Rename printer` : t`Remove printer?`}
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
                <label htmlFor={`printer-name-${printer.id}`}><Trans>Printer name</Trans></label>
                <input
                  id={`printer-name-${printer.id}`}
                  ref={input}
                  value={name}
                  onChange={(event) => setName(event.target.value)}
                />
                {problem && <p role="alert">{problem}</p>}
                <div className="printer-dialog-buttons">
                  <button type="button" className="button-secondary" onClick={closeDialog}>
                    <Trans>Cancel</Trans>
                  </button>
                  <button type="submit" className="button-primary" disabled={problem !== undefined}>
                    <Trans>Save</Trans>
                  </button>
                </div>
              </form>
            ) : (
              <>
                <p>
                  <Trans>Remove “{printerName}”? Its printer settings will be deleted. Your projects stay.</Trans>
                </p>
                <div className="printer-dialog-buttons">
                  <button type="button" className="button-secondary" onClick={closeDialog}>
                    <Trans>Cancel</Trans>
                  </button>
                  <button
                    type="button"
                    className="button-danger"
                    onClick={() => {
                      actions.onRemove(printer.id);
                      closeDialog();
                    }}
                  >
                    <Trans>Remove</Trans>
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
