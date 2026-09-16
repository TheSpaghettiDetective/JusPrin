// Typed view of the jusprin-home-bridge protocol. The canonical shared source
// is resources/jusprin/home/protocol.json; the constants here are derived from
// it at build time so the page and the native host cannot silently disagree
// about the protocol name or version.
//
// The page owns its own chrome strings, as the Agent page does. The host sends
// data plus the few lines only it can phrase: a project's status text and a
// printer's connection and nozzle lines come from native code that already has
// the device facts and the translations.

import protocolJson from '@resources/jusprin/home/protocol.json';

export const PROTOCOL_NAME: string = protocolJson.name;
export const PROTOCOL_VERSION: number = protocolJson.version;
export const PAGE_CAPABILITIES: string[] = protocolJson.capabilities;

export type PageMessageType =
  | 'hello'
  | 'state_request'
  | 'open_project'
  | 'new_project'
  | 'import_project'
  | 'launch_monitor'
  | 'add_printer'
  | 'open_printer_settings'
  | 'rename_printer'
  | 'remove_printer';

export type HostMessageType =
  | 'hello_ack'
  | 'hello_reject'
  | 'state'
  | 'projects'
  | 'printers'
  | 'appearance'
  | 'printer_error'
  | 'bridge_error';

export type Appearance = 'light' | 'dark';

// What a project card's status line means, separate from what it says. Only
// 'printing' earns a colored dot; every other state takes the plain line, so
// an idle project cannot be mistaken for a running one.
//
// 'unknown' is honest about the current state of the codebase: nothing records
// per-project slice state yet, so the host sends what it can derive (the
// recent-file modified time) under this kind. When a real store exists, the
// host starts sending the other kinds and this page needs no change.
export type ProjectStatusKind = 'unknown' | 'draft' | 'sliced' | 'printing' | 'completed';

export interface ProjectStatus {
  kind: ProjectStatusKind;
  // Host-formatted and already translated; the page never composes it.
  text: string;
}

export interface ProjectInfo {
  id: string;
  name: string;
  // Shown only as a tooltip: the card is titled by name.
  path: string;
  // A file: URL the host resolved from the .3mf thumbnail, absent when the
  // project carries none.
  thumbnailUrl?: string;
  status: ProjectStatus;
}

export type PrinterState = 'idle' | 'printing' | 'offline';

export interface SpoolInfo {
  // '#RRGGBB', from the fork-owned spool store.
  colour: string;
}

// A named printer is one the person added and names in JusPrin: this page
// confirms its rename and removal. A device is a Bambu printer no named
// printer stands for: its rename and removal open Orca's own dialogs, which
// confirm for themselves.
export type PrinterKind = 'named' | 'device';

export interface PrinterInfo {
  id: string;
  name: string;
  kind: PrinterKind;
  // The card menu's items; one the host sends as false is shown disabled.
  canOpenSettings: boolean;
  canRename: boolean;
  canRemove: boolean;
  state: PrinterState;
  // Host-formatted status line, e.g. "Printing · 43% · 2h left". Present
  // whenever the host has something to say about the current job.
  statusText?: string;
  // 0-100, present only while printing; drives the progress bar's width.
  progressPercent?: number;
  connectionText?: string;
  nozzleText?: string;
  materialLabel?: string;
  spools: SpoolInfo[];
  canLaunchMonitor: boolean;
}

// A printer action the host understood but could not carry out, such as a
// name another profile already has. The message is translated.
export interface PrinterErrorPayload {
  id: string;
  message: string;
}

export interface StatePayload {
  appearance: Appearance;
  projects: ProjectInfo[];
  printers: PrinterInfo[];
}

export interface Envelope<T = unknown> {
  protocol: string;
  version: number;
  id: string;
  type: string;
  correlationId?: string;
  payload: T;
}

export function isEnvelope(value: unknown): value is Envelope {
  if (typeof value !== 'object' || value === null) return false;
  const env = value as Record<string, unknown>;
  return (
    env.protocol === PROTOCOL_NAME &&
    typeof env.version === 'number' &&
    typeof env.id === 'string' &&
    typeof env.type === 'string'
  );
}
