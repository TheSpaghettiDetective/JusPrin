// The Agent page in its Preferences mode (?panel=preferences): JusPrin's
// Preferences window. It speaks the same bridge as the Agent panel, so its
// key checks and AI tool connections run on the same host code, and adds one
// message of its own, preferences_action.

import { useEffect, useMemo, useReducer } from 'react';
import { BridgeClient, Transport } from './bridge/client';
import { initialState, reducer } from './state/store';
import { applyAppearance } from './tokens';
import { BridgeErrorPane, ConnectingPane } from './components/Panels';
import { Preferences, PreferencesActions } from './components/Preferences';

export function PreferencesApp({ getTransport }: { getTransport: () => Transport | null }) {
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

  const actions = useMemo<PreferencesActions>(
    () => ({
      set: (name, value) => client.send('preferences_action', { action: 'set', name, value }),
      run: (action, details) => client.send('preferences_action', { ...details, action }),
      checkKey: (provider, apiKey) => client.send('setup_check_key', { provider, apiKey }),
      cancelCheck: () => client.send('setup_cancel', {}),
      removeKey: (provider) => client.send('setup_remove_key', { provider }),
      refreshTools: () => client.send('mcp_catalog', {}),
      previewTool: (toolId) => client.send('mcp_preview', { toolId }),
      connectTool: (toolId) => client.send('mcp_connect', { toolId }),
      revealTool: (toolId) => client.send('reveal_path', { toolId }),
      signIn: () => client.send('account_sign_in', {}),
      cancelSignIn: () => client.send('account_cancel', {}),
    }),
    [client],
  );

  if (state.connection !== 'connected' && state.connection !== 'connecting') {
    return (
      <div className="app">
        <BridgeErrorPane title="Preferences could not reach JusPrin" detail={state.connectionDetail}
          diagnostics={state.diagnostics} onRetry={() => client.retry()} />
      </div>
    );
  }
  if (!state.preferences) return <div className="app"><ConnectingPane /></div>;

  return (
    <div className="app app--preferences">
      <Preferences
        preferences={state.preferences}
        setup={state.setup}
        savedKeys={state.savedKeys}
        account={state.account}
        mcpCatalog={state.mcpCatalog}
        mcpPreview={state.mcpPreview}
        mcpStatus={state.mcpStatus}
        actions={actions}
      />
    </div>
  );
}
