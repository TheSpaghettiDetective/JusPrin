// Home's state and the one reducer that advances it. The page renders what the
// host last said; it owns no project or printer state of its own, so a reload
// is always recoverable from the next `state` envelope.

import type { ConnectionState } from '../bridge/client';
import type {
  Appearance,
  ConfirmPayload,
  Envelope,
  HistoryEntry,
  HistoryPayload,
  PrinterErrorPayload,
  PrinterInfo,
  ProjectInfo,
  ScreenPayload,
  StatePayload,
  OnboardingState,
  PrintReportPhoto,
  PrintReportResult,
} from '../bridge/protocol';

// Home's two screens. Print history is always a leaf: Back returns to the
// gallery when the gallery opened it, and to the project when the project did.
// Where it was opened from sets its first filter.
export type Screen =
  | { kind: 'home' }
  | { kind: 'history'; back: 'home' | 'project'; projectId?: string; printerId?: string; focus?: string };

export interface HomeState {
  connection: ConnectionState;
  connectionDetail?: string;
  // Absent until the host answers the handshake with state: an empty gallery
  // and "no projects yet" are different screens.
  loaded: boolean;
  appearance: Appearance;
  projects: ProjectInfo[];
  // Why there are no projects to show, when they could not be read.
  projectsProblem?: string;
  printers: PrinterInfo[];
  error?: string;
  // The last printer action the host refused. The host follows a refusal with
  // a fresh state, so only the person's next printer action clears it.
  printerError?: PrinterErrorPayload;
  // A printer the conversation just added, which the host leads the column
  // with for this one push; its card draws attention to itself once.
  highlightPrinter?: string;
  // The host's open question, until the person answers it.
  confirmation?: ConfirmPayload;
  screen: Screen;
  // The Print history's entries, as the host last sent them; absent until
  // the first arrives. The page asks again each time the screen opens and
  // each time the host's state changes under it.
  history?: HistoryEntry[];
  reportsOffered?: boolean;
  // The print report form's photo and the server's answer, while it is open.
  reportPhoto?: PrintReportPhoto;
  reportResult?: PrintReportResult;
  // Counts the host's state pushes, so the open history can follow them.
  stateCount: number;
  onboarding?: OnboardingState;
  onboardingError?: string;
}

export const initialState: HomeState = {
  connection: 'connecting',
  loaded: false,
  appearance: 'light',
  projects: [],
  printers: [],
  screen: { kind: 'home' },
  stateCount: 0,
};

export type HomeAction =
  | { kind: 'connection'; state: ConnectionState; detail?: string }
  | { kind: 'envelope'; envelope: Envelope }
  | { kind: 'printer_action' }
  | { kind: 'confirmation_answered' }
  | { kind: 'show'; screen: Screen }
  | { kind: 'report_form_reset' }
  | { kind: 'report_sending' }
  | { kind: 'onboarding_action' };

export function reduce(state: HomeState, action: HomeAction): HomeState {
  if (action.kind === 'connection') {
    return { ...state, connection: action.state, connectionDetail: action.detail };
  }
  if (action.kind === 'printer_action') {
    return { ...state, printerError: undefined };
  }
  if (action.kind === 'confirmation_answered') {
    return { ...state, confirmation: undefined };
  }
  if (action.kind === 'show') {
    return { ...state, screen: action.screen };
  }
  if (action.kind === 'report_form_reset') {
    return { ...state, reportPhoto: undefined, reportResult: undefined };
  }
  if (action.kind === 'report_sending') {
    return { ...state, reportResult: undefined };
  }
  if (action.kind === 'onboarding_action') {
    return { ...state, onboardingError: undefined };
  }
  const { type, payload } = action.envelope;
  switch (type) {
    case 'state': {
      const next = payload as StatePayload;
      return {
        ...state,
        loaded: true,
        appearance: next.appearance,
        projects: next.projects ?? [],
        projectsProblem: next.projectsProblem || undefined,
        printers: next.printers ?? [],
        error: undefined,
        highlightPrinter: next.highlightPrinter || undefined,
        stateCount: state.stateCount + 1,
        onboarding: next.onboarding,
      };
    }
    case 'screen': {
      const asked = payload as ScreenPayload;
      return {
        ...state,
        screen:
          asked.screen === 'history'
            ? { kind: 'history', back: asked.back, projectId: asked.projectId, focus: asked.focus }
            : { kind: 'home' },
      };
    }
    case 'history':
      return {
        ...state,
        history: (payload as HistoryPayload).entries ?? [],
        reportsOffered: (payload as HistoryPayload).reportsOffered,
      };
    case 'print_report_photo':
      // Cancelling the file dialog keeps the photo already there.
      return { ...state, reportPhoto: (payload as { photo?: PrintReportPhoto }).photo ?? state.reportPhoto };
    case 'print_report_result':
      return { ...state, reportResult: payload as PrintReportResult };
    case 'projects':
      return { ...state, projects: (payload as { projects: ProjectInfo[] }).projects ?? [] };
    case 'printers':
      return { ...state, printers: (payload as { printers: PrinterInfo[] }).printers ?? [] };
    case 'appearance':
      return { ...state, appearance: (payload as { appearance: Appearance }).appearance };
    case 'printer_error':
      return { ...state, printerError: payload as PrinterErrorPayload };
    case 'confirm':
      return { ...state, confirmation: payload as ConfirmPayload };
    case 'onboarding_error':
      return { ...state, onboardingError: (payload as { message?: string }).message };
    case 'bridge_error':
      return { ...state, error: (payload as { message?: string }).message };
    default:
      return state;
  }
}
