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
const embedded = new URLSearchParams(window.location.search).get('embedded') === '1';

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <App getTransport={nativeTransport} embedded={embedded} />
  </StrictMode>,
);
