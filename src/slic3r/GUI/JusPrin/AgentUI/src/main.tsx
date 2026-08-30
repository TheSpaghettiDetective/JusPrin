import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import { App } from './App';
import { nativeTransport } from './bridge/client';
import { applyAppearance } from './tokens';
import './styles.css';

applyAppearance('light');

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <App getTransport={nativeTransport} />
  </StrictMode>,
);
