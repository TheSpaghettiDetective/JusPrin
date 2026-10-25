// The Agent page in its welcome-back mode (?panel=welcome): the window shown
// once after JusPrin v1's setup was imported. It speaks the same bridge as
// Preferences, so the account v1 was signed in to is checked and signed in to
// again by the same host code.

import { useEffect, useMemo, useReducer } from 'react';
import { BridgeClient, Transport } from './bridge/client';
import { initialState, reducer } from './state/store';
import { applyAppearance } from './tokens';
import { BridgeErrorPane, ConnectingPane } from './components/Panels';
import { WelcomeBack, WelcomeBackActions } from './components/WelcomeBack';

export function WelcomeApp({ getTransport }: { getTransport: () => Transport | null }) {
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

  const actions = useMemo<WelcomeBackActions>(
    () => ({
      checkAccount: () => client.send('account_check', {}),
      signIn: () => client.send('account_sign_in', {}),
      cancelSignIn: () => client.send('account_cancel', {}),
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
  if (!state.welcomeBack) return <div className="app"><ConnectingPane /></div>;

  return (
    <div className="app app--welcome">
      <WelcomeBack report={state.welcomeBack} account={state.savedKeys.find((key) => key.provider === 'jusprin')}
        status={state.account} actions={actions} />
    </div>
  );
}
