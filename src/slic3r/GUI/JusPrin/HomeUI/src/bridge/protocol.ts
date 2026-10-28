// Typed view of the jusprin-home-bridge protocol. The canonical shared source
// is resources/jusprin/home/protocol.json; the constants here are derived from
// it at build time so the page and the native host cannot silently disagree
// about the protocol name or version.
//
// The page owns its own chrome strings, as the Agent page does. The host sends
// data plus the few lines only it can phrase: a project's status text and a
// printer's connection and model lines come from native code that already has
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
  | 'open_file'
  | 'open_dropped_files'
  | 'launch_monitor'
  | 'add_printer'
  | 'connect_printer'
  | 'calibrate_printer'
  | 'open_printer_settings'
  | 'rename_printer'
  | 'remove_printer'
  | 'confirm_answer'
  | 'open_preferences'
  | 'history_request'
  | 'history_back'
  | 'open_in_chat'
  | 'export_gcode'
  | 'print_again';

export type HostMessageType =
  | 'hello_ack'
  | 'hello_reject'
  | 'state'
  | 'projects'
  | 'printers'
  | 'appearance'
  | 'printer_error'
  | 'bridge_error'
  | 'confirm'
  | 'screen'
  | 'history';

export type Appearance = 'light' | 'dark';

// What a project card's status line means, separate from what it says. Only
// 'printing' earns a colored dot; every other state takes the plain line, so
// an idle project cannot be mistaken for a running one.
//
// 'unknown' means the project's records could not be read; its text is empty
// and the card shows no line.
export type ProjectStatusKind = 'unknown' | 'draft' | 'sent' | 'printing' | 'completed';

export interface ProjectStatus {
  kind: ProjectStatusKind;
  // Host-formatted and already translated; the page never composes it.
  text: string;
}

// What a card says about its print history, as counts the page words. A
// G-code is one that left a project; a print is one a printer reported
// starting. Nothing records a failure: a print that stopped early simply has
// no finish.
export interface HistorySummary {
  gcodes: number;
  exports: number;
  sends: number;
  prints: number;
  printingNow: number;
  // The newest finish, ISO 8601 in UTC.
  lastFinishedAt?: string;
}

export interface ProjectInfo {
  id: string;
  name: string;
  // Shown only as a tooltip: the card is titled by name.
  path: string;
  // A preview URL, absent when the project carries none.
  thumbnailUrl?: string;
  // When the model was last saved, ISO 8601 in UTC; the card words it.
  updatedAt?: string;
  status: ProjectStatus;
  // Absent when nothing ever left the project.
  history?: HistorySummary;
}

export type PrinterState = 'idle' | 'printing' | 'offline';

// A spool the printer holds, as a connected printer reports its tray. Either
// field may be absent; the card names what it knows and draws a swatch only
// for a colour.
export interface SpoolInfo {
  // The filament type, such as 'PLA'.
  material?: string;
  // '#RRGGBB'.
  colour?: string;
}

// What the connection line rests on. 'online' is a Bambu device's;
// 'connected' is a print host with a saved address (for Moonraker, one that
// answered lately); 'offline' is a Bambu device or a Moonraker host that has
// gone silent; 'none' is neither, and draws no dot.
export type ConnectionState = 'none' | 'online' | 'offline' | 'connected';

// How the printer is reached; 'host' is Moonraker or OctoPrint.
export type ConnectionKind = 'lan' | 'cloud' | 'host';

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
  connectionState: ConnectionState;
  // Host-formatted Connection row, e.g. "Online · LAN", "Not connected".
  connectionText?: string;
  connectionKind?: ConnectionKind;
  connectionAction?: 'reconnect';
  // A print host's address as saved, without its scheme.
  address?: string;
  // Host-formatted Model row, e.g. "X1 Carbon · 0.4 mm".
  modelText?: string;
  spools: SpoolInfo[];
  // A Bambu card's Launch monitor, or a print host's Open printer window.
  canLaunchMonitor: boolean;
  // What was made for, sent to and printed on this printer, across projects;
  // absent when nothing was.
  history?: HistorySummary;
}

// Which screen the page shows, as the host asks when Home comes on screen:
// the gallery, or a Print history the project opened, filtered to it, which
// Back leaves for the project again.
export type ScreenPayload =
  | { screen: 'home' }
  | { screen: 'history'; projectId?: string; focus?: string; back: 'project' };

// Where a G-code went. An exported file is checked against what was written
// each time the screen asks; a send is never verified like a file.
export type HistoryDestination =
  | { id: string; kind: 'export'; at: string; path: string; check: 'matches' | 'changed' | 'missing' }
  | {
      id: string;
      kind: 'send';
      at: string;
      printerName: string;
      // The printer card it went to, when Home has one.
      printerId?: string;
      // The transport reported the file on the printer.
      delivered: boolean;
      // The send also asked the printer to start.
      startRequested: boolean;
    };

// A print, only as the printer reported it.
export interface HistoryPrint {
  id: string;
  printerName: string;
  printerId?: string;
  startedAt: string;
  finishedAt?: string;
  // Present while the printer is running this print.
  live?: { percent?: number; remainingSeconds?: number };
}

// One G-code that left a project: the screen's entry.
export interface HistoryEntry {
  projectId: string;
  projectName: string;
  // SHA-256 of the G-code; the page shows its first eight digits.
  hash: string;
  // The exported file's name, else the plate's.
  name: string;
  plateName: string;
  createdAt: string;
  printTimeSeconds?: number;
  grams?: number;
  layers?: number;
  // The presets it was made with, as the person sees them.
  printer?: string;
  material?: string;
  process?: string;
  // The chat and marker where it first went out.
  conversationId?: string;
  markerId?: string;
  // Whether the project still keeps an intact copy to export.
  kept: boolean;
  // Whether Print again can send the kept file to the printer selected now.
  // 'not_for_this_printer': kept, but not as that printer takes it (a Bambu
  // printer takes the package only a Bambu send keeps). 'changed': the kept
  // file no longer holds what was kept, and is never sent.
  printAgain: 'ready' | 'not_kept' | 'not_for_this_printer' | 'no_printer' | 'changed';
  // Every printer card it belongs to: made for, sent to or printed on.
  printerIds: string[];
  destinations: HistoryDestination[];
  prints: HistoryPrint[];
}

export interface HistoryPayload {
  entries: HistoryEntry[];
}

// A printer action the host understood but could not carry out, such as a
// name another profile already has. The message is translated.
export interface PrinterErrorPayload {
  id: string;
  message: string;
}


// A question the host asks before an action it cannot take back, drawn as
// the design system's Confirmation dialog: a calibration that saves and
// closes the open project. The answer goes back with its requestId.
export interface ConfirmPayload {
  requestId: string;
  title: string;
  body: string;
  confirm: string;
  cancel: string;
}

export interface StatePayload {
  appearance: Appearance;
  projects: ProjectInfo[];
  // Host-formatted and already translated; non-empty when the saved projects
  // could not be read, which the gallery says instead of its empty-gallery line.
  projectsProblem?: string;
  printers: PrinterInfo[];
  // A printer the conversation just added; empty for any other push.
  highlightPrinter?: string;
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
