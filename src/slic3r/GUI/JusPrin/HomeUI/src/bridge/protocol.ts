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
  | 'import_project'
  | 'launch_monitor'
  | 'add_printer'
  | 'connect_printer'
  | 'open_printer_settings'
  | 'rename_printer'
  | 'remove_printer'
  | 'onboarding_begin'
  | 'onboarding_dismiss'
  | 'onboarding_defer_profiles'
  | 'onboarding_accept_partial_profiles'
  | 'onboarding_use_profiles'
  | 'onboarding_confirm_setup'
  | 'onboarding_back'
  | 'onboarding_choose_profile_bundle'
  | 'onboarding_choose_profile_folder'
  | 'onboarding_manual_setup'
  | 'onboarding_offline_example'
  | 'onboarding_account_stub'
  | 'onboarding_terms_stub'
  | 'onboarding_privacy_stub'
  | 'onboarding_open_example';

export type HostMessageType =
  | 'hello_ack'
  | 'hello_reject'
  | 'state'
  | 'projects'
  | 'printers'
  | 'appearance'
  | 'printer_error'
  | 'onboarding_error'
  | 'bridge_error';

export type Appearance = 'light' | 'dark';

// What a project card's status line means, separate from what it says. Only
// 'printing' earns a colored dot; every other state takes the plain line, so
// an idle project cannot be mistaken for a running one.
//
// 'unknown' means the local project store has no current slice state to report.
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
  // A preview URL, absent when the project carries none.
  thumbnailUrl?: string;
  status: ProjectStatus;
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
  // A printer the conversation just added; empty for any other push.
  highlightPrinter?: string;
  onboarding: OnboardingState;
}

export type OnboardingStep = 'hidden' | 'welcome' | 'profiles' | 'confirm_setup' | 'setup' | 'project';

export interface OnboardingState {
  visible: boolean;
  step: OnboardingStep;
  status: 'unfinished' | 'completed' | 'dismissed';
  agentConfigured: boolean;
  profiles: {
    available: boolean;
    imported: boolean;
    partialImportPending: boolean;
    failed: string[];
    printerCount: number;
    filamentCount: number;
    processCount: number;
    source: string;
    // Preset names found in the source, per kind, for reviewing a selection.
    printerNames?: string[];
    filamentNames?: string[];
    processNames?: string[];
    // After an import that left some presets behind: the ones that were
    // copied, named like `failed` as "<directory>/<file>.json".
    importedFiles?: string[];
  };
  setup: {
    usable: boolean;
    offlineExample: boolean;
    summary: string;
    printer: string;
    nozzle: string;
    plate: string;
    material: string;
    process: string;
  };
  projectOpen: boolean;
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
