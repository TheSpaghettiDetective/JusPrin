import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import { App } from './App';
import { PreferencesApp } from './PreferencesApp';
import { WelcomeApp } from './WelcomeApp';
import { nativeTransport } from './bridge/client';
import { applyAppearance, applyStaticTokens } from './tokens';
import './styles.css';

applyStaticTokens();
applyAppearance('light');

const query = new URLSearchParams(window.location.search);
// The temporary printer task chat loads the same page with ?panel=printer: the same
// thread and composer, with the printer session's card pinned above them
// instead of the project's setup card. See App.tsx.
const panel = query.get('panel') === 'printer';
// JusPrin's Preferences window loads it with ?panel=preferences: its own
// surface, drawn with the same Agent connection rows. See PreferencesApp.tsx.
const preferences = query.get('panel') === 'preferences';
// The window shown once after JusPrin v1's setup was imported loads it with
// ?panel=welcome. See WelcomeApp.tsx.
const welcome = query.get('panel') === 'welcome';

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    {preferences
      ? <PreferencesApp getTransport={nativeTransport} />
      : welcome
        ? <WelcomeApp getTransport={nativeTransport} />
        : <App getTransport={nativeTransport} printerPanel={panel} />}
  </StrictMode>,
);
