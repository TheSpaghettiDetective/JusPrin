// First: it activates the language ?lang= names before any other module runs.
import '../../AgentUI/src/i18n';
import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import { App } from './App';
import { nativeTransport } from './bridge/client';
import { applyAppearance, applyStaticTokens } from './tokens';
import './styles.css';

applyStaticTokens();
applyAppearance('light');

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <App getTransport={nativeTransport} />
  </StrictMode>,
);
