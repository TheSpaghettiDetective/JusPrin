// Home's state and the one reducer that advances it. The page renders what the
// host last said; it owns no project or printer state of its own, so a reload
// is always recoverable from the next `state` envelope.

import type { ConnectionState } from '../bridge/client';
import type { Appearance, Envelope, PrinterInfo, ProjectInfo, StatePayload } from '../bridge/protocol';

export interface HomeState {
  connection: ConnectionState;
  connectionDetail?: string;
  // Absent until the host answers the handshake with state: an empty gallery
  // and "no projects yet" are different screens.
  loaded: boolean;
  appearance: Appearance;
  projects: ProjectInfo[];
  printers: PrinterInfo[];
  error?: string;
}

export const initialState: HomeState = {
  connection: 'connecting',
  loaded: false,
  appearance: 'light',
  projects: [],
  printers: [],
};

export type HomeAction =
  | { kind: 'connection'; state: ConnectionState; detail?: string }
  | { kind: 'envelope'; envelope: Envelope };

export function reduce(state: HomeState, action: HomeAction): HomeState {
  if (action.kind === 'connection') {
    return { ...state, connection: action.state, connectionDetail: action.detail };
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
        error: undefined,
      };
    }
    case 'projects':
      return { ...state, projects: (payload as { projects: ProjectInfo[] }).projects ?? [] };
    case 'printers':
      return { ...state, printers: (payload as { printers: PrinterInfo[] }).printers ?? [] };
    case 'appearance':
      return { ...state, appearance: (payload as { appearance: Appearance }).appearance };
    case 'bridge_error':
      return { ...state, error: (payload as { message?: string }).message };
    default:
      return state;
  }
}
