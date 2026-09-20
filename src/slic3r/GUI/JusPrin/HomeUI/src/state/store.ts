// Home's state and the one reducer that advances it. The page renders what the
// host last said; it owns no project or printer state of its own, so a reload
// is always recoverable from the next `state` envelope.

import type { ConnectionState } from '../bridge/client';
import type {
  AddedPrinterInfo,
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
  // A successful Add's receipt: leads the column with this printer (already
  // reordered by the host, for that one push) and says what it assumed.
  // Lives only through the `state` push it rode in on -- the very next
  // `state`, for any reason, clears it, since by then the reorder is gone
  // too and the facts it states may no longer hold. The person's dismiss
  // clears it early; a newer Add's `printer_added` replaces it.
  addedPrinter?: AddedPrinterInfo;
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
  | { kind: 'printer_action' }
  | { kind: 'dismiss_added_printer' };

export function reduce(state: HomeState, action: HomeAction): HomeState {
  if (action.kind === 'connection') {
    return { ...state, connection: action.state, connectionDetail: action.detail };
  }
  if (action.kind === 'printer_action') {
    return { ...state, printerError: undefined };
  }
  if (action.kind === 'dismiss_added_printer') {
    return { ...state, addedPrinter: undefined };
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
        // The receipt states facts as of the push it rode in on (the host
        // already leads `printers` with that entry for that one push); any
        // later `state` means those facts may be stale, so the receipt
        // (and the reorder, and the highlight it drives) end here. The
        // host sends `printer_added` right after this same envelope when
        // it is the push a receipt belongs to, so a fresh one still lands.
        addedPrinter: undefined,
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
    case 'printer_added':
      return { ...state, addedPrinter: payload as AddedPrinterInfo };
    case 'bridge_error':
      return { ...state, error: (payload as { message?: string }).message };
    default:
      return state;
  }
}
