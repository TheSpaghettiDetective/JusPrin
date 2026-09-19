import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import { App } from './App';
import { nativeTransport } from './bridge/client';
import { applyAppearance, applyStaticTokens } from './tokens';
import './styles.css';

applyStaticTokens();
applyAppearance('light');

// A throwaway, setup-only host (e.g. embedded in the Add a printer dialog)
// loads this same page with ?embedded=1 so it renders only the setup
// sub-component instead of the full conversation chrome. See App.tsx.
const query = new URLSearchParams(window.location.search);
const embedded = query.get('embedded') === '1';
// The printer panel on Home loads the same page with ?panel=printer: the same
// thread and composer, with the printer session's card pinned above them
// instead of the project's setup card. See App.tsx.
const panel = query.get('panel') === 'printer';

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <App getTransport={nativeTransport} embedded={embedded} printerPanel={panel} />
  </StrictMode>,
);
