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
import type { CreditAccount, PrinterFormInfo } from '../../../AgentUI/src/bridge/protocol';

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
  | 'ask_about_printer_settings'
  | 'rename_printer'
  | 'remove_printer'
  | 'confirm_answer'
  | 'open_preferences'
  | 'history_request'
  | 'history_back'
  | 'open_in_chat'
  | 'export_gcode'
  | 'print_again'
  | 'print_report_choose_photo'
  | 'print_report_send'
  | 'onboarding_sign_in'
  | 'onboarding_sign_in_cancel'
  | 'onboarding_sign_in_email'
  | 'onboarding_sign_in_code'
  | 'onboarding_sign_in_password'
  | 'onboarding_sign_in_send_code'
  | 'onboarding_sign_in_reopen'
  | 'onboarding_open_link'
  | 'onboarding_open_preferences'
  | 'onboarding_leave_welcome'
  | 'onboarding_back'
  | 'onboarding_start_fresh'
  | 'onboarding_import'
  | 'onboarding_import_continue'
  | 'onboarding_assume_default'
  | 'onboarding_printer_form'
  | 'agent_card_dismiss';

export type HostMessageType =
  | 'hello_ack'
  | 'hello_reject'
  | 'state'
  | 'projects'
  | 'printers'
  | 'appearance'
  | 'printer_error'
  | 'onboarding_error'
  | 'bridge_error'
  | 'confirm'
  | 'screen'
  | 'history'
  | 'print_report_photo'
  | 'print_report_result';

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
  // The card asks how its latest finished print came out.
  reportDue?: boolean;
}

// What a card says the printer is doing, in one word. Each rests on
// something the host read: 'not_connected' when it has no connection or none
// answers, 'connecting' before the first answer, 'connected' when the printer
// answers without saying what it is doing, and the rest when it says.
export type PrinterStatus =
  | 'not_connected'
  | 'connecting'
  | 'connected'
  | 'ready'
  | 'printing'
  | 'paused'
  | 'attention';

// Set on a 'ready' card when the printer says its last print finished.
export type LastPrint = 'finished';

// The card's one button: a Bambu printer's monitor, a print host's own page
// in a window, Reconnect, or Connect for a printer with no connection.
export type PrinterAction = 'none' | 'monitor' | 'printer_window' | 'reconnect' | 'connect';

// A camera the card can show: an MJPEG stream, or a picture to fetch again.
export interface PrinterCamera {
  streamUrl?: string;
  snapshotUrl?: string;
}

// A spool the printer holds, as a connected printer reports its tray. Either
// field may be absent; the card names what it knows and draws a swatch only
// for a colour.
export interface SpoolInfo {
  // The filament type, such as 'PLA'.
  material?: string;
  // '#RRGGBB'.
  colour?: string;
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
  status: PrinterStatus;
  // The job while one runs or is paused, as the printer names it.
  job?: string;
  // 0-100 and the seconds left, while a job runs or is paused; each absent
  // when the printer does not say.
  progressPercent?: number;
  secondsLeft?: number;
  // The printer's own words about what needs doing, or why a card is not
  // connected.
  message?: string;
  lastPrint?: LastPrint;
  // When the app last heard from a printer that is not connected, ISO 8601
  // in UTC; absent when it does not know.
  lastConnectedAt?: string;
  camera?: PrinterCamera;
  // The facts line's parts: how it is reached ('LAN', 'Klipper' ...), a
  // print host's address as saved, and "X1 Carbon · 0.4 mm".
  connectionLabel?: string;
  address?: string;
  modelText?: string;
  // The default the first launch put in place when the person skipped
  // choosing: the card marks it "assumed".
  assumed?: boolean;
  // Added by the person, with no connection ever set up: the card says what
  // connecting is for in place of a status.
  noConnection?: boolean;
  spools: SpoolInfo[];
  action: PrinterAction;
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
  // The report the person sent: that it exists and what it earned, never how
  // the print went.
  report?: { sentAt: string; credits: number };
}

export interface HistoryPayload {
  entries: HistoryEntry[];
  // Reports earn credits, so they need the person's account.
  reportsOffered?: boolean;
}

export interface PrintReportPhoto {
  id: string;
  name: string;
  sizeBytes: number;
  // A data URL; absent when the host could not draw the file.
  thumbnail?: string;
}

export type PrintReportOutcome = 'worked' | 'partly' | 'failed';

export interface PrintReportResult {
  hash: string;
  state: 'sent' | 'already_sent' | 'failed';
  credits: number;
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
  onboarding: OnboardingState;
}

// The first launch: the welcome, then the Printers step, then Home. The host
// decides which is in front; the page only draws it.
export type OnboardingStep = 'hidden' | 'welcome' | 'printers';

// The sign-in the welcome's first door started: 'browser' while the person
// finishes it there, 'email' while the email door runs, 'checking' while the
// app reads the account either one brought.
export type SignInPhase = 'idle' | 'browser' | 'email' | 'checking';

// The server's refusal of the last code, password or request for a code.
export type EmailRefusal = '' | 'wrong_code' | 'expired_code' | 'too_many_tries' | 'too_many_codes' | 'wrong_password';

// The email door while signIn is 'email': the person picks an emailed code
// or the password ('choose'), or the app waits for the code ('code').
// 'expired_code' is a code that no longer works: it expired or was tried too
// often.
export interface EmailSignIn {
  step: 'choose' | 'code';
  email: string;
  // A code, a password or a request for a code is with the server.
  busy: boolean;
  refusal: EmailRefusal;
  // A new code went out because the person asked for one.
  codeResent: boolean;
}

// One file an OrcaSlicer import brought, or could not. renamedTo is set for
// a copy imported under a new name because JusPrin had the name; reason,
// already worded, for one not imported.
export interface ImportResultItem {
  kind: 'printer' | 'filament' | 'process';
  name: string;
  renamedTo: string;
  imported: boolean;
  reason: string;
}

export interface OnboardingState {
  visible: boolean;
  step: OnboardingStep;
  // Bring OrcaSlicer's printers, what that import brought, or the
  // add-printer form.
  printersScreen: 'orca_import' | 'import_result' | 'add_printer';
  signIn: SignInPhase;
  email: EmailSignIn | null;
  // A JusPrin v1 import ran: the offer says v1 subscribers keep their price.
  v1Imported: boolean;
  // Why the last sign-in did not finish, already worded.
  signInError: string;
  // Set once the sign-in is saved: the welcome's card says who signed in.
  accountEmail: string;
  agentConfigured: boolean;
  // The provider whose saved key the agent runs on, as "Anthropic"; empty
  // when it runs on the account, or there is no agent.
  agentKeyProvider: string;
  // The OrcaSlicer folder the Printers step offers to bring printers from;
  // selected is the one of its printers OrcaSlicer has selected, or empty;
  // found is false when none with printers was found, and importResult is
  // set while the import's result is on screen.
  orca: { source: string; printers: string[]; selected: string; filamentCount: number; found: boolean;
    importResult: ImportResultItem[] | null };
  // The add-printer form, as the printer conversation describes it; null
  // until its catalogue is read.
  printerForm: PrinterFormInfo | null;
  // What Home's Agent group shows: the three doors while no agent is set
  // up, the account and its credits once after signing in, else nothing.
  agentCard: 'doors' | 'account' | 'none';
  // The account and its credits; null until the gateway answers.
  account: CreditAccount | null;
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
