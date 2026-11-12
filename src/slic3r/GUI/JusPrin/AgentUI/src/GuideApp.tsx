// The Agent page in its guide mode (?panel=guide): the window Help › JusPrin
// guide… opens (Shell/GuideWindow.cpp). It reads nothing from the app but its
// appearance; a link out of the guide is a shell_action the window opens in
// the browser.

import { useEffect, useMemo, useReducer } from 'react';
import { BridgeClient, Transport } from './bridge/client';
import { initialState, reducer } from './state/store';
import { applyAppearance } from './tokens';
import { Guide } from './components/Guide';
import { guidePages } from './guidePages';

export function GuideApp({ getTransport }: { getTransport: () => Transport | null }) {
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

  // The guide is in the page itself, so it shows even before the app answers.
  return (
    <div className="app app--guide">
      <Guide pages={guidePages} openLink={(url) => client.send('shell_action', { action: 'open_link', url })} />
    </div>
  );
}
