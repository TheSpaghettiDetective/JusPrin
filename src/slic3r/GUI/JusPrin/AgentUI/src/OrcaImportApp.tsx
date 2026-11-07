// The Agent page in its OrcaSlicer import mode (?panel=orca-import): the
// window File › Import › Import from OrcaSlicer… opens. It speaks the same
// bridge as the welcome-back window; each button is a shell_action the
// window carries out (Home/OrcaImportWindow.cpp).

import { useEffect, useMemo, useReducer } from 'react';
import { BridgeClient, Transport } from './bridge/client';
import { initialState, reducer } from './state/store';
import { applyAppearance } from './tokens';
import { BridgeErrorPane, ConnectingPane } from './components/Panels';
import { OrcaImport, OrcaImportActions } from './components/OrcaImport';

export function OrcaImportApp({ getTransport }: { getTransport: () => Transport | null }) {
  const [state, dispatch] = useReducer(reducer, initialState);

  const client = useMemo(
    () =>
      new BridgeClient({
        getTransport,
        onConnectionChange: (connection, detail) => dispatch({ kind: 'connection', state: connection, detail }),
        onEnvelope: (envelope) => dispatch({ kind: 'host', envelope }),
      }),
    // The transport is fixed for the lifetime of the page.
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [],
  );

  useEffect(() => {
    client.start();
  }, [client]);

  useEffect(() => {
    applyAppearance(state.appearance);
  }, [state.appearance]);

  const actions = useMemo<OrcaImportActions>(
    () => ({
      importPrinters: (printers) => client.send('shell_action', { action: 'import', printers }),
      chooseFolder: () => client.send('shell_action', { action: 'choose_folder' }),
      close: () => client.send('shell_action', { action: 'close' }),
    }),
    [client],
  );

  if (state.connection !== 'connected' && state.connection !== 'connecting') {
    return (
      <div className="app">
        <BridgeErrorPane title="This window could not reach JusPrin" detail={state.connectionDetail}
          diagnostics={state.diagnostics} onRetry={() => client.retry()} />
      </div>
    );
  }
  if (!state.orcaImport) return <div className="app"><ConnectingPane /></div>;

  return (
    <div className="app app--orca-import">
      <OrcaImport state={state.orcaImport} actions={actions} />
    </div>
  );
}
