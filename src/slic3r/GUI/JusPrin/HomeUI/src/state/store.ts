// Home's state and the one reducer that advances it. The page renders what the
// host last said; it owns no project or printer state of its own, so a reload
// is always recoverable from the next `state` envelope.

import type { ConnectionState } from '../bridge/client';
import type {
  Appearance,
  Envelope,
  PrinterErrorPayload,
  PrinterInfo,
  ProjectInfo,
  StatePayload,
} from '../bridge/protocol';

export interface HomeState {
  connection: ConnectionState;
  connectionDetail?: string;
  // Absent until the host answers the handshake with state: an empty gallery
  // and "no projects yet" are different screens.
  loaded: boolean;
  appearance: Appearance;
  projects: ProjectInfo[];
  printers: PrinterInfo[];
  // The printer conversation is open in place of the printers column.
  printerPanelOpen: boolean;
  error?: string;
  // The last printer action the host refused. The host follows a refusal with
  // a fresh state, so only the person's next printer action clears it.
  printerError?: PrinterErrorPayload;
}

export const initialState: HomeState = {
  connection: 'connecting',
  loaded: false,
  appearance: 'light',
  projects: [],
  printers: [],
  printerPanelOpen: false,
};

export type HomeAction =
  | { kind: 'connection'; state: ConnectionState; detail?: string }
  | { kind: 'envelope'; envelope: Envelope }
  | { kind: 'printer_action' };

export function reduce(state: HomeState, action: HomeAction): HomeState {
  if (action.kind === 'connection') {
    return { ...state, connection: action.state, connectionDetail: action.detail };
  }
  if (action.kind === 'printer_action') {
    return { ...state, printerError: undefined };
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
        printers: next.printers ?? [],
        printerPanelOpen: next.printerPanelOpen ?? false,
        error: undefined,
      };
    }
    case 'projects':
      return { ...state, projects: (payload as { projects: ProjectInfo[] }).projects ?? [] };
    case 'printers':
      return { ...state, printers: (payload as { printers: PrinterInfo[] }).printers ?? [] };
    case 'appearance':
      return { ...state, appearance: (payload as { appearance: Appearance }).appearance };
    case 'printer_panel':
      return { ...state, printerPanelOpen: (payload as { open: boolean }).open };
    case 'printer_error':
      return { ...state, printerError: payload as PrinterErrorPayload };
    case 'bridge_error':
      return { ...state, error: (payload as { message?: string }).message };
    default:
      return state;
  }
}
