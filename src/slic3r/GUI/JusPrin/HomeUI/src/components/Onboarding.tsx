// The first launch: the welcome, then the Printers step. Each screen is the
// board of the same name on the "JusPrin v2" canvas's "On boarding
// experience" page (first-run-welcome-01, first-run-04 to 08, 18 to 20). The host
// decides which is in front (Home/OnboardingModel) and carries out every
// door; this file only draws them.
//
// Every door on the welcome comes back into the sequence: signing in waits on
// the offer card, Preferences closes back onto the welcome, and "Just slice
// for now" moves on at once.

import { useEffect, useRef, useState } from 'react';
import type { Appearance, OnboardingState } from '../bridge/protocol';
import { afterPaint } from '@shared/afterPaint';
import { SIGNUP_AFTER, SIGNUP_OFFER, SIGNUP_SCALE, signedInCredits } from '../../../AgentUI/src/credits';
import { OrcaImportResultLines, OrcaPrinterList } from '../../../AgentUI/src/components/OrcaImportLists';
import clip from '@resources/jusprin/welcome/welcome-clip.mp4';
import { SignInDoor as SharedSignInDoor, SignInActions } from '../../../AgentUI/src/components/SignInDoor';
import { CheckGlyph, FolderGlyph } from './Glyphs';
import { Lockup } from './Lockup';
import { PrinterPickerHost } from './PrinterPickerHost';
import type { PrinterPick } from '../../../AgentUI/src/components/PrinterPicker';

// What Skip installs: OrcaSlicer's Generic Klipper Printer, its 250 × 250 mm
// bed and Klipper G-code (OrcaHomeBackend::assume_default_printer). Home's
// "No printer yet" card and its drop zone say the same.
export const STAND_IN_PRINTER = 'Until you add yours, files are sliced for a 250 mm Klipper printer and may not print on other printers.';
// The same before Skip, under Continue and Skip for now.
const SKIP_PRINTER = 'If you skip, files are sliced for a stand-in printer with a 250 mm bed, not for yours. Add your printer before you '
  + 'print anything. You can do that later on the home screen.';

// How long the card says who signed in before the Printers step.
const SIGNED_IN_PAUSE_MS = 1500;

type Action = (type: string, payload?: object) => void;

// Home's own buttons, drawn in the shared sign-in.
const HOME_BUTTONS = { primary: 'button-primary', secondary: 'button-secondary' };

interface Props {
  onboarding: OnboardingState;
  appearance: Appearance;
  error?: string;
  onAction: Action;
}

// Who signed in, under what signing in got them: the free credits a new
// account was just given, or what an account used before has left.
function SignedIn({ onboarding }: { onboarding: OnboardingState }) {
  const account = onboarding.account;
  if (!account) return <p className="onboarding-door-title" role="status">Signed in as {onboarding.accountEmail}</p>;
  return (
    <div role="status">
      <p className="onboarding-door-title">{signedInCredits(account)}</p>
      <p className="onboarding-door-detail">Signed in as {onboarding.accountEmail}</p>
    </div>
  );
}

// The first door, on the welcome and in Home's AI group alike: the sign-up
// offer over the JusPrin sign-in every screen shares (AgentUI's SignInDoor),
// and who signed in. Returns the words and controls only; the caller frames
// them.
export function SignInDoor({ onboarding, onAction, offer }: { onboarding: OnboardingState; onAction: Action; offer?: boolean }) {
  if (onboarding.signIn === 'idle' && onboarding.accountEmail) return <SignedIn onboarding={onboarding} />;
  const actions: SignInActions = {
    google: () => onAction('onboarding_sign_in'),
    email: (address) => onAction('onboarding_sign_in_email', { email: address }),
    sendCode: () => onAction('onboarding_sign_in_send_code'),
    code: (code) => onAction('onboarding_sign_in_code', { code }),
    password: (password) => onAction('onboarding_sign_in_password', { password }),
    cancel: () => onAction('onboarding_sign_in_cancel'),
    reopen: () => onAction('onboarding_sign_in_reopen'),
    openLink: (link) => onAction('onboarding_open_link', { link }),
    getNewVersion: () => onAction('check_for_updates'),
  };
  return (
    <SharedSignInDoor phase={onboarding.signIn} email={onboarding.email} error={onboarding.signInError}
      outdated={onboarding.signInOutdated} actions={actions} buttons={HOME_BUTTONS}>
      {offer && <p className="onboarding-door-title">{SIGNUP_OFFER}</p>}
      {offer && <p className="onboarding-door-detail">{SIGNUP_SCALE}</p>}
      {offer && (
        <p className="onboarding-door-detail">
          {SIGNUP_AFTER}{' '}
          {/* The server's Credits and plans, in the app's sheet. */}
          <button type="button" className="sign-in-link inline" onClick={() => onAction('onboarding_see_prices')}>See prices</button>
        </p>
      )}
    </SharedSignInDoor>
  );
}

// The agent on the person's own AI account, an API key or Claude Code and
// Claude Desktop: Preferences › Agent explains both, and closes back here.
// A card of its own under the offer, with what it costs.
export function OwnAccountDoor({ onAction, disabled }: { onAction: Action; disabled: boolean }) {
  return (
    <button type="button" className="onboarding-door" disabled={disabled}
      onClick={() => onAction('onboarding_open_preferences', { door: 'key' })}>
      <span className="onboarding-door-text">
        <span className="onboarding-door-name">Use your own AI account</span>
        <span className="onboarding-door-what">Your API key, or Claude Code or Claude Desktop</span>
      </span>
      <span className="onboarding-door-cost">Free</span>
    </button>
  );
}

function Welcome({ onboarding, appearance, onAction }: Props) {
  const signingIn = onboarding.signIn !== 'idle';
  // The other doors stay open while the person signs in, as canvas boards
  // first-run-04 and 12 to 16 draw them, and close only while the account is read.
  const checking = onboarding.signIn === 'checking';
  // Only a sign-in finished here moves on by itself. Someone who comes Back
  // already signed in sees who, and goes on with Continue.
  const waited = useRef(false);
  if (signingIn) waited.current = true;
  const signedIn = Boolean(onboarding.accountEmail) && !signingIn;
  useEffect(() => {
    if (!signedIn || !waited.current) return;
    const timer = setTimeout(() => onAction('onboarding_leave_welcome'), SIGNED_IN_PAUSE_MS);
    return () => clearTimeout(timer);
  }, [signedIn, onAction]);

  return (
    <div className="onboarding">
      <Lockup appearance={appearance} className="onboarding-lockup" />
      <div className="onboarding-welcome">
        <figure className="onboarding-clip">
          <video src={clip} autoPlay loop muted playsInline aria-label="AI fixing a print that came out stringy" />
          <details className="onboarding-credits">
            <summary>Photo credits</summary>
            <p>
              “Dual extrusion 3D printed #3DBenchy v01” by #3DBenchy, flickr.com/photos/3dbenchy/19273965123, CC BY 2.0
              (creativecommons.org/licenses/by/2.0). Cropped.
            </p>
            <p>
              “Dual-print multi-part color 3D print of #3DBenchy v13” by #3DBenchy, flickr.com/photos/3dbenchy/19239738473,
              CC BY 2.0 (creativecommons.org/licenses/by/2.0). Cropped and resized.
            </p>
          </details>
        </figure>
        <div className="onboarding-pitch">
          <div className="onboarding-heading">
            <h1>The AI slicer that helps you get better prints.</h1>
            <p className="onboarding-lead">
              Show it a photo of what went wrong. AI reads your file, your settings and your slice, changes the settings
              and slices again.
            </p>
          </div>
          {/* Once signed in there is nothing left to choose: only the credits
              card, with Continue, stays. */}
          <div className="onboarding-doors">
            {!signedIn && <p className="onboarding-doors-label">Sign up to use AI, or skip it and just slice.</p>}
            <div className="onboarding-offer">
              <SignInDoor onboarding={onboarding} onAction={onAction} offer />
              {signedIn && !waited.current && (
                <button type="button" className="button-primary onboarding-door-button"
                  onClick={() => onAction('onboarding_leave_welcome')}>
                  Continue
                </button>
              )}
            </div>
            {!signedIn && (
              <>
                <OwnAccountDoor onAction={onAction} disabled={checking} />
                <button type="button" className="onboarding-link flush" disabled={checking}
                  onClick={() => onAction('onboarding_leave_welcome')}>
                  Just slice for now
                </button>
              </>
            )}
          </div>
        </div>
      </div>
    </div>
  );
}

// What the welcome finished, above the first printer screen's headline:
// who signed in, or whose key the agent uses. Nothing after "Just slice for
// now".
function WelcomeDone({ onboarding }: { onboarding: OnboardingState }) {
  const { account, accountEmail } = onboarding;
  if (accountEmail && account) {
    return (
      <div className="onboarding-done">
        <CheckGlyph />
        <span className="onboarding-done-text">
          <span>{signedInCredits(account)}</span>
          <span className="onboarding-done-detail">Signed in as {accountEmail}</span>
        </span>
      </div>
    );
  }
  const text = accountEmail ? `Signed in as ${accountEmail}`
    : onboarding.agentKeyProvider ? `AI uses your ${onboarding.agentKeyProvider} key` : '';
  if (!text) return null;
  return <p className="onboarding-done"><CheckGlyph />{text}</p>;
}

// Importing holds the app on the action's message, so the button says so
// first and the message goes once that has been painted. The host answers
// every action with fresh state.
function useBusy(onAction: Action, onboarding: OnboardingState) {
  const [busy, setBusy] = useState(false);
  useEffect(() => setBusy(false), [onboarding]);
  const run = (type: string, payload?: object) => {
    setBusy(true);
    afterPaint(() => onAction(type, payload));
  };
  return [busy, run] as const;
}

// The apps the Printers step imports from, by the host's id.
const IMPORT_APPS = { jusprin1: 'JusPrin 1', orcaslicer: 'OrcaSlicer' } as const;
type Way = keyof typeof IMPORT_APPS | 'printer';

// The other two of the three ways to set up printers, under every printer
// screen. The host opens the way's screen, or first asks for the app's
// folder when it found none.
function OtherWays({ ways, disabled, onAction }: { ways: Way[]; disabled?: boolean; onAction: Action }) {
  return (
    <div className="onboarding-ways">
      <span className="onboarding-ways-label">Other ways:</span>
      {ways.map((way) => (
        <button key={way} type="button" className="onboarding-link" disabled={disabled}
          onClick={() => onAction('onboarding_way', { way })}>
          {way === 'printer' ? 'Choose my printer' : `Import from ${IMPORT_APPS[way]}…`}
        </button>
      ))}
    </div>
  );
}

// Why the last way's folder did not open its screen.
function WayProblem({ onboarding }: { onboarding: OnboardingState }) {
  return onboarding.wayProblem ? <p className="onboarding-error" role="alert">{onboarding.wayProblem}</p> : null;
}

const countOf = (count: number, one: string, many: string) => `${count} ${count === 1 ? one : many}`;

// Bring the printers of JusPrin 1 (first-run-19) or OrcaSlicer (first-run-07).
function PrinterImport({ onboarding, error, onAction }: Props) {
  const found = onboarding.printerImport;
  const id = found.app === 'jusprin1' ? 'jusprin1' : 'orcaslicer';
  const app = IMPORT_APPS[id];
  const [busy, run] = useBusy(onAction, onboarding);
  // Every printer starts ticked; only the ticked ones are imported.
  const [chosen, setChosen] = useState(found.printers);
  const listed = found.printers.join('\n');
  useEffect(() => setChosen(found.printers), [listed]);
  const choose = (name: string, ticked: boolean) =>
    setChosen((now) => (ticked ? [...now, name] : now.filter((one) => one !== name)));
  const brings = [found.filamentCount > 0 && countOf(found.filamentCount, 'filament', 'filaments'),
    found.settingsCount > 0 && countOf(found.settingsCount, 'print setting', 'print settings')].filter(Boolean).join(' and ');
  return (
    <div className="onboarding-step">
      <WelcomeDone onboarding={onboarding} />
      <div className="onboarding-heading">
        <h1>Bring your printers from {app}?</h1>
        <p className="onboarding-lead">
          JusPrin copies these printers with their filaments and print settings. {app} keeps its own copy.
        </p>
      </div>
      <OrcaPrinterList app={app} printers={found.printers.map((name) => ({ name, already: false }))} chosen={chosen}
        onChoose={choose} disabled={busy} />
      <div className="onboarding-source">
        {brings && <p>Also brings {brings}.</p>}
        <p className="onboarding-source-folder">
          <FolderGlyph />
          <span className="onboarding-source-path">{app} keeps these in {found.source}</span>
        </p>
      </div>
      {error && <p className="onboarding-error" role="alert">{error}</p>}
      <div className="onboarding-actions">
        <button type="button" className="button-primary" disabled={busy || chosen.length === 0}
          onClick={() => run('onboarding_import', { app: id, printers: chosen })}>
          {busy ? 'Importing…' : 'Import'}
        </button>
      </div>
      <WayProblem onboarding={onboarding} />
      <OtherWays ways={[id === 'jusprin1' ? 'orcaslicer' : 'jusprin1', 'printer']} disabled={busy} onAction={onAction} />
      <button type="button" className="onboarding-link flush" disabled={busy} onClick={() => onAction('onboarding_back')}>Back</button>
    </div>
  );
}

// A line not imported says what to do about it, trying again included.
function ImportResult({ onboarding, error, onAction }: Props) {
  const items = onboarding.printerImport.result ?? [];
  const app = IMPORT_APPS[onboarding.printerImport.app === 'jusprin1' ? 'jusprin1' : 'orcaslicer'];
  return (
    <div className="onboarding-step">
      <div className="onboarding-heading">
        <h1>Imported from {app}</h1>
      </div>
      <OrcaImportResultLines app={app} lines={items} />
      {error && <p className="onboarding-error" role="alert">{error}</p>}
      <div className="onboarding-actions">
        <button type="button" className="button-primary" onClick={() => onAction('onboarding_import_continue')}>
          Continue
        </button>
      </div>
    </div>
  );
}

// "Which printer do you have?": the printer and its nozzle, added with its
// profile's own plate and filament. The dialog Home's "+ Add printer" opens
// asks for those too.
function ChoosePrinter({ onboarding, error, onAction }: Props) {
  const form = onboarding.printerForm;
  const [pick, setPick] = useState<PrinterPick>({});
  const { catalogId, nozzle, deviceId } = pick;
  // Continue waits for the app: it either leaves for Home or says why not.
  const [finishing, setFinishing] = useState(false);
  useEffect(() => { setFinishing(false); }, [form?.error]);
  useEffect(() => {
    if (catalogId && nozzle !== undefined) onAction('onboarding_printer_form', { action: 'form_options', catalogId, nozzle });
  }, [catalogId, nozzle]);
  // The options the app sent for exactly this choice, and none for another.
  const options = form?.options && form.options.catalogId === catalogId && form.options.nozzle === nozzle ? form.options : undefined;
  const ready = options !== undefined && !form?.loading && !options.error;
  // The agent can help only when it can answer: an account with credits, or a key.
  const agentReady = onboarding.agentKeyProvider !== '' ||
    (onboarding.accountEmail !== '' && (onboarding.account?.credits.total ?? 0) > 0);

  const finish = () => {
    if (!options || !catalogId || nozzle === undefined) return;
    const plate = options.plates.some((entry) => entry.type === options.defaultPlate) ? options.defaultPlate : options.plates[0]?.type;
    setFinishing(true);
    onAction('onboarding_printer_form', { action: 'form_finish', catalogId, nozzle,
      ...(options.plates.length > 0 ? { plate } : {}), ...(deviceId ? { deviceId } : {}) });
  };

  return (
    <div className="onboarding-step">
      <WelcomeDone onboarding={onboarding} />
      <div className="onboarding-heading">
        <h1>Which printer do you have?</h1>
        <p className="onboarding-lead">
          JusPrin prepares every print to fit your printer’s bed and nozzle. You can add more printers later.
        </p>
      </div>
      {form ? (
        <PrinterPickerHost form={form} pick={pick} onPick={setPick}
          reading={Boolean(form.loading && catalogId && nozzle !== undefined)}
          onAskAgent={agentReady ? () => onAction('onboarding_ask_agent') : undefined} />
      ) : (
        <p className="onboarding-lead" role="status">Reading the printer list…</p>
      )}
      {options?.error && <p className="onboarding-error" role="alert">{options.error}</p>}
      {form?.error && <p className="onboarding-error" role="alert">{form.error}</p>}
      {error && <p className="onboarding-error" role="alert">{error}</p>}
      <div className="onboarding-step-group">
        <div className="onboarding-actions">
          <button type="button" className="button-primary" disabled={!ready || finishing} onClick={finish}>Continue</button>
          <button type="button" className="onboarding-link" onClick={() => onAction('onboarding_assume_default')}>Skip for now</button>
        </div>
        {!catalogId && <p className="onboarding-note">{SKIP_PRINTER}</p>}
      </div>
      <WayProblem onboarding={onboarding} />
      <OtherWays ways={['jusprin1', 'orcaslicer']} onAction={onAction} />
      <button type="button" className="onboarding-link flush" onClick={() => onAction('onboarding_back')}>Back</button>
    </div>
  );
}

export function Onboarding(props: Props) {
  if (props.onboarding.step === 'welcome') return <Welcome {...props} />;
  return (
    <div className="onboarding">
      <Lockup appearance={props.appearance} className="onboarding-lockup" />
      {props.onboarding.printersScreen === 'v1_import' || props.onboarding.printersScreen === 'orca_import'
        ? <PrinterImport {...props} />
        : props.onboarding.printersScreen === 'import_result' ? <ImportResult {...props} />
          : <ChoosePrinter {...props} />}
    </div>
  );
}
