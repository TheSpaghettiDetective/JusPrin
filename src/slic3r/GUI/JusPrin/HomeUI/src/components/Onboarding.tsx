// The first launch: the welcome, then the Printers step. Each screen is the
// board of the same name on the "JusPrin v2" canvas's "On boarding
// experience" page (first-run-welcome-01, first-run-04 to 08). The host
// decides which is in front (Home/OnboardingModel) and carries out every
// door; this file only draws them.
//
// Every door on the welcome comes back into the sequence: signing in waits on
// the offer card, Preferences closes back onto the welcome, and "Just slice
// for now" moves on at once.

import { useEffect, useRef, useState } from 'react';
import type { Appearance, EmailSignIn, OnboardingState } from '../bridge/protocol';
import { afterPaint } from '@shared/afterPaint';
import { SIGNUP_AFTER, SIGNUP_OFFER, SIGNUP_SCALE, signedInCredits } from '../../../AgentUI/src/credits';
import { OrcaImportResultLines, OrcaPrinterList } from '../../../AgentUI/src/components/OrcaImportLists';
import lockupLight from '@resources/jusprin/overlay/images/OrcaSlicer_horizontal_light.svg';
import lockupDark from '@resources/jusprin/overlay/images/OrcaSlicer_horizontal_dark.svg';
import clip from '@resources/jusprin/welcome/welcome-clip.mp4';
import googleMark from '@resources/jusprin/welcome/google-g.svg';
import { CodeField } from './CodeField';
import { CheckGlyph, FolderGlyph } from './Glyphs';
import { PrinterPickerHost } from './PrinterPickerHost';
import type { PrinterPick } from '../../../AgentUI/src/components/PrinterPicker';

// What Skip installs: OrcaSlicer's Generic Klipper Printer, its 250 × 250 mm
// bed and Klipper G-code (OrcaHomeBackend::assume_default_printer). Home's
// "No printer yet" card and its drop zone say the same.
export const STAND_IN_PRINTER = 'Until you add yours, files are sliced for a 250 mm Klipper printer and may not print on other printers.';
// The same before Skip, under Continue and Skip for now.
const SKIP_PRINTER = 'If you skip, files are sliced for a 250 mm Klipper printer until you add yours, and may not print on other printers.';

// How long the card says who signed in before the Printers step.
const SIGNED_IN_PAUSE_MS = 1500;

type Action = (type: string, payload?: object) => void;

interface Props {
  onboarding: OnboardingState;
  appearance: Appearance;
  error?: string;
  onAction: Action;
}

// The JusPrin lockup in the window's top-left corner, as the welcome boards
// anchor it; the welcome has no header of its own.
function Lockup({ appearance }: { appearance: Appearance }) {
  return <img className="onboarding-lockup" src={appearance === 'dark' ? lockupDark : lockupLight} alt="JusPrin" />;
}

// What the server's refusal means to the person.
function refusalText(email: EmailSignIn): string {
  switch (email.refusal) {
    case 'wrong_code':
      return 'That code is wrong.';
    case 'expired_code':
      return 'That code no longer works. We can send a new one.';
    case 'too_many_tries':
      return 'Too many tries. Wait a few minutes, or email me a code.';
    case 'too_many_codes':
      return 'Too many codes were sent to this address. Try again in an hour.';
    case 'wrong_password':
      return 'That password is wrong.';
    default:
      return '';
  }
}

// Links in a row, separated by middle dots, with Cancel apart on the right.
function LinkRow({ links, cancel }: { links: [string, () => void][]; cancel?: () => void }) {
  return (
    <div className="onboarding-link-row">
      <span className="onboarding-links">
        {links.map(([label, onClick], index) => (
          <span key={label}>
            {index > 0 && <span className="onboarding-link-dot" aria-hidden="true">·</span>}
            <button type="button" className="onboarding-link flush" onClick={onClick}>{label}</button>
          </span>
        ))}
      </span>
      {cancel && <button type="button" className="onboarding-link flush" onClick={cancel}>Cancel</button>}
    </div>
  );
}

// The email door's code step (canvas boards first-run-14 to 16).
function CodeStep({ email, onAction, onChangeEmail, onBrowser }: {
  email: EmailSignIn; onAction: Action; onChangeEmail: () => void; onBrowser: () => void;
}) {
  const [code, setCode] = useState('');
  const field = useRef<HTMLInputElement>(null);
  const refusal = refusalText(email);
  // A code that can no longer work: the way on is a new one.
  const spent = email.refusal === 'expired_code';
  const sendCode = () => onAction('onboarding_sign_in_send_code');
  // A new code empties the cells; so does typing after a wrong one.
  useEffect(() => {
    if (email.codeResent) setCode('');
  }, [email.codeResent]);
  useEffect(() => {
    if (email.refusal === 'wrong_code' && !email.busy) setCode('');
  }, [email.refusal, email.busy]);
  useEffect(() => {
    if (!email.busy) field.current?.focus();
  }, [email.busy]);
  return (
    <>
      <p className="onboarding-door-title">Check your email.</p>
      <p className="onboarding-door-detail">
        {email.codeResent ? 'We sent a new code to ' : 'We sent a 6-digit code to '}
        {email.email}.
      </p>
      <CodeField
        ref={field}
        value={code}
        refused={email.refusal === 'wrong_code' || spent}
        disabled={email.busy}
        onChange={setCode}
        onComplete={(digits) => onAction('onboarding_sign_in_code', { code: digits })}
      />
      {refusal ? (
        <p className="onboarding-error" role="alert">{refusal}</p>
      ) : (
        <p className="onboarding-door-note">Type the code from the email. You can open it on your phone.</p>
      )}
      {spent ? (
        <div className="onboarding-link-row onboarding-new-code-row">
          <button type="button" className="button-primary" disabled={email.busy} onClick={sendCode}>Send a new code</button>
          <LinkRow links={[['Change email', onChangeEmail], ['Sign in with Google instead', onBrowser]]} />
        </div>
      ) : (
        <LinkRow
          links={[['Send a new code', sendCode], ['Change email', onChangeEmail], ['Sign in with Google instead', onBrowser]]}
          cancel={() => onAction('onboarding_sign_in_cancel')}
        />
      )}
    </>
  );
}

// The email door before anything is sent: an emailed code, which also
// makes the account of an address that has none, and, for someone who set
// one, the password behind "Use a password instead".
function ChooseStep({ email, onAction, onChangeEmail }: { email: EmailSignIn; onAction: Action; onChangeEmail: () => void }) {
  const [password, setPassword] = useState('');
  const [withPassword, setWithPassword] = useState(email.refusal === 'wrong_password');
  const refusal = refusalText(email);
  const codeRefused = email.refusal === 'too_many_codes';
  return (
    <>
      <p className="onboarding-door-title">We’ll email you a code.</p>
      <p className="onboarding-door-detail">
        {email.email}.{' '}
        <button type="button" className="onboarding-link inline" onClick={onChangeEmail}>Change email</button>
      </p>
      <button type="button" className="button-primary onboarding-code-button" disabled={email.busy}
        onClick={() => onAction('onboarding_sign_in_send_code')}>
        Email me a 6-digit code
      </button>
      {codeRefused && <p className="onboarding-error" role="alert">{refusal}</p>}
      {withPassword ? (
        <>
          <p className="onboarding-or"><span>or</span></p>
          <form className="onboarding-email-row" onSubmit={(event) => {
            event.preventDefault();
            if (password) onAction('onboarding_sign_in_password', { password });
          }}>
            <input className="onboarding-field" type="password" placeholder="Password" autoComplete="current-password"
              aria-label="Password" value={password} disabled={email.busy} autoFocus
              onChange={(event) => setPassword(event.target.value)} />
            <button type="submit" className="button-secondary" disabled={email.busy || !password}>Sign in</button>
          </form>
          {refusal && !codeRefused && <p className="onboarding-error" role="alert">{refusal}</p>}
          <LinkRow
            links={[['Forgot password', () => onAction('onboarding_open_link', { link: 'reset_password' })]]}
            cancel={() => onAction('onboarding_sign_in_cancel')}
          />
        </>
      ) : (
        <LinkRow links={[['Use a password instead', () => setWithPassword(true)]]} cancel={() => onAction('onboarding_sign_in_cancel')} />
      )}
    </>
  );
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

// The first door, on the welcome and in Home's Agent group alike: the
// sign-up offer, Google in the browser or an email that stays in the app,
// each step of either, and who signed in. Signing up and signing in are
// the same door: an address with no account gets one from its first code,
// so "Sign up free" sends it at once, and only "Sign in" asks how (board
// first-run-11). Returns the words and controls only; the caller frames them.
export function SignInDoor({ onboarding, onAction, offer }: { onboarding: OnboardingState; onAction: Action; offer?: boolean }) {
  // The address typed at rest, kept for "Change email".
  const [address, setAddress] = useState('');
  const addressField = useRef<HTMLInputElement>(null);
  const [focusAddress, setFocusAddress] = useState(false);
  // "Sign up free" was pressed: the code is on its way, so the step that asks
  // how to sign in is not shown while the server sends it.
  const [sendingCode, setSendingCode] = useState(false);
  useEffect(() => {
    if (focusAddress && onboarding.signIn === 'idle') {
      addressField.current?.focus();
      setFocusAddress(false);
    }
  }, [focusAddress, onboarding.signIn]);
  const backToAddress = () => {
    onAction('onboarding_sign_in_cancel');
    setFocusAddress(true);
  };
  const browser = () => {
    onAction('onboarding_sign_in_cancel');
    onAction('onboarding_sign_in');
  };

  const email = onboarding.email;
  if (onboarding.signIn === 'email' && email && email.step === 'code')
    return <CodeStep email={email} onAction={onAction} onChangeEmail={backToAddress} onBrowser={browser} />;
  if (onboarding.signIn === 'email' && email && sendingCode && !email.refusal) {
    return (
      <>
        <p className="onboarding-door-title onboarding-waiting">
          <span className="onboarding-spinner" aria-hidden="true" />
          Sending a code to {email.email}…
        </p>
        <LinkRow links={[]} cancel={() => onAction('onboarding_sign_in_cancel')} />
      </>
    );
  }
  if (onboarding.signIn === 'email' && email)
    return <ChooseStep email={email} onAction={onAction} onChangeEmail={backToAddress} />;
  if (onboarding.signIn === 'browser') {
    return (
      <>
        <p className="onboarding-door-title onboarding-waiting">
          <span className="onboarding-spinner" aria-hidden="true" />
          Finish in your browser.
        </p>
        <p className="onboarding-door-detail">The app continues on its own once the browser is done.</p>
        <p className="onboarding-door-note">
          Nothing happened?{' '}
          <button type="button" className="onboarding-link inline" onClick={() => onAction('onboarding_sign_in_reopen')}>
            Open the browser again
          </button>
        </p>
        <LinkRow links={[['Use an email code instead', backToAddress]]} cancel={() => onAction('onboarding_sign_in_cancel')} />
      </>
    );
  }
  if (onboarding.signIn === 'checking') {
    return (
      <>
        <p className="onboarding-door-title onboarding-waiting">
          <span className="onboarding-spinner" aria-hidden="true" />
          Signing you in…
        </p>
        <LinkRow links={[]} cancel={() => onAction('onboarding_sign_in_cancel')} />
      </>
    );
  }
  if (onboarding.accountEmail) return <SignedIn onboarding={onboarding} />;
  // At rest. The host takes the address, then the code request, in order.
  const sendEmail = (withCode: boolean) => {
    const typed = address.trim();
    if (!typed) {
      addressField.current?.focus();
      return;
    }
    setSendingCode(withCode);
    onAction('onboarding_sign_in_email', { email: typed });
    if (withCode) onAction('onboarding_sign_in_send_code');
  };
  return (
    <>
      {offer && <p className="onboarding-door-title">{SIGNUP_OFFER}</p>}
      {offer && <p className="onboarding-door-detail">{SIGNUP_SCALE}</p>}
      {offer && <p className="onboarding-door-detail">{SIGNUP_AFTER}</p>}
      {offer && onboarding.v1Imported && (
        <p className="onboarding-door-detail">Subscribed to JusPrin v1? You get these 500 credits and keep your original price.</p>
      )}
      <button type="button" className="button-secondary onboarding-google"
        onClick={() => onAction('onboarding_sign_in')}>
        <img src={googleMark} alt="" />
        Continue with Google
      </button>
      <p className="onboarding-or"><span>or</span></p>
      <form className="onboarding-email-row" onSubmit={(event) => {
        event.preventDefault();
        sendEmail(true);
      }}>
        <input ref={addressField} className="onboarding-field" type="email" placeholder="you@example.com" autoComplete="email"
          aria-label="Email" value={address} onChange={(event) => setAddress(event.target.value)} />
        <button type="submit" className="button-primary" disabled={!address.trim()}>Sign up free</button>
      </form>
      <p className="onboarding-door-note">
        Already have an account?{' '}
        <button type="button" className="onboarding-link inline" onClick={() => sendEmail(false)}>Sign in</button>
      </p>
      <p className="onboarding-door-note">
        By continuing you agree to the{' '}
        <button type="button" className="onboarding-link inline" onClick={() => onAction('onboarding_open_link', { link: 'terms' })}>
          Terms
        </button>{' '}
        and{' '}
        <button type="button" className="onboarding-link inline" onClick={() => onAction('onboarding_open_link', { link: 'privacy' })}>
          Privacy Policy
        </button>
        .
      </p>
      {onboarding.signInError && <p className="onboarding-error" role="alert">{onboarding.signInError}</p>}
    </>
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
      <Lockup appearance={appearance} />
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
            <h1>The AI slicer that fixes failed prints.</h1>
            <p className="onboarding-lead">
              Show it a photo of what went wrong. AI reads your file, your settings and your slice, changes the settings
              and slices again.
            </p>
          </div>
          <div className="onboarding-doors">
            <p className="onboarding-doors-label">Sign up to use AI, or skip it and just slice.</p>
            <div className="onboarding-offer">
              <SignInDoor onboarding={onboarding} onAction={onAction} offer />
              {signedIn && !waited.current && (
                <button type="button" className="button-primary onboarding-door-button"
                  onClick={() => onAction('onboarding_leave_welcome')}>
                  Continue
                </button>
              )}
            </div>
            <OwnAccountDoor onAction={onAction} disabled={checking || (signedIn && waited.current)} />
            <button type="button" className="onboarding-link flush" disabled={checking || (signedIn && waited.current)}
              onClick={() => onAction('onboarding_leave_welcome')}>
              Just slice for now
            </button>
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

// The printer JusPrin selects once the import is done, as the importer
// picks it: OrcaSlicer's own, while it is ticked, otherwise the first ticked.
function startsWith(orca: OnboardingState['orca'], chosen: string[]): string {
  const ticked = orca.printers.filter((name) => chosen.includes(name));
  if (ticked.length === 0) return '';
  if (ticked.includes(orca.selected)) return `JusPrin starts with ${orca.selected}, the printer OrcaSlicer had selected.`;
  return `JusPrin starts with ${ticked[0]}.`;
}

function OrcaImport({ onboarding, error, onAction }: Props) {
  const { orca } = onboarding;
  const [busy, run] = useBusy(onAction, onboarding);
  // Every printer starts ticked; only the ticked ones are imported.
  const [chosen, setChosen] = useState(orca.printers);
  const listed = orca.printers.join('\n');
  useEffect(() => setChosen(orca.printers), [listed]);
  const choose = (name: string, ticked: boolean) =>
    setChosen((now) => (ticked ? [...now, name] : now.filter((one) => one !== name)));
  const starts = startsWith(orca, chosen);
  return (
    <div className="onboarding-step">
      <WelcomeDone onboarding={onboarding} />
      <div className="onboarding-heading">
        <h1>Bring your printers from OrcaSlicer?</h1>
        <p className="onboarding-lead">
          JusPrin copies these printers with their filaments, settings and printer connections. OrcaSlicer keeps its own copy.
        </p>
      </div>
      <OrcaPrinterList printers={orca.printers.map((name) => ({ name, already: false }))} chosen={chosen} onChoose={choose}
        disabled={busy} />
      <div className="onboarding-source">
        {orca.filamentCount > 0 && (
          <p>Also brings {orca.filamentCount === 1 ? '1 filament' : `${orca.filamentCount} filaments`}.</p>
        )}
        <p className="onboarding-source-folder">
          <FolderGlyph />
          <span className="onboarding-source-path">{orca.source}</span>
        </p>
      </div>
      {starts && <p className="onboarding-lead">{starts}</p>}
      {error && <p className="onboarding-error" role="alert">{error}</p>}
      <div className="onboarding-actions">
        <button type="button" className="button-primary" disabled={busy || chosen.length === 0}
          onClick={() => run('onboarding_import', { printers: chosen })}>
          {busy ? 'Importing…' : 'Import'}
        </button>
        <button type="button" className="button-secondary" disabled={busy} onClick={() => onAction('onboarding_start_fresh')}>
          Choose my printer instead
        </button>
      </div>
      <button type="button" className="onboarding-link flush" disabled={busy} onClick={() => onAction('onboarding_back')}>Back</button>
    </div>
  );
}

// A line not imported says what to do about it, trying again included.
function ImportResult({ onboarding, error, onAction }: Props) {
  const items = onboarding.orca.importResult ?? [];
  return (
    <div className="onboarding-step">
      <div className="onboarding-heading">
        <h1>Imported from OrcaSlicer</h1>
      </div>
      <OrcaImportResultLines lines={items} />
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
      {!onboarding.orca.found && <WelcomeDone onboarding={onboarding} />}
      <div className="onboarding-heading">
        <h1>Which printer do you have?</h1>
        <p className="onboarding-lead">
          JusPrin plans every print for your printer: its bed, its nozzle and the filament it starts with. You can add more
          printers later.
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
      <button type="button" className="onboarding-link flush" onClick={() => onAction('onboarding_back')}>Back</button>
    </div>
  );
}

export function Onboarding(props: Props) {
  if (props.onboarding.step === 'welcome') return <Welcome {...props} />;
  return (
    <div className="onboarding">
      <Lockup appearance={props.appearance} />
      {props.onboarding.printersScreen === 'orca_import' ? <OrcaImport {...props} />
        : props.onboarding.printersScreen === 'import_result' ? <ImportResult {...props} />
          : <ChoosePrinter {...props} />}
    </div>
  );
}
