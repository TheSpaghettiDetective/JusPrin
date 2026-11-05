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
import type { Appearance, EmailSignIn, ImportResultItem, OnboardingState } from '../bridge/protocol';
import { creditCount } from '../../../AgentUI/src/credits';
import lockupLight from '@resources/jusprin/overlay/images/OrcaSlicer_horizontal_light.svg';
import lockupDark from '@resources/jusprin/overlay/images/OrcaSlicer_horizontal_dark.svg';
import clip from '@resources/jusprin/welcome/welcome-clip.mp4';
import googleMark from '@resources/jusprin/welcome/google-g.svg';
import { CodeField } from './CodeField';
import { CheckGlyph, FolderGlyph, PrinterGlyph, WarningGlyph } from './Glyphs';
import { PrinterFormHost } from './PrinterFormHost';

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
        <p className="onboarding-door-note">The sixth digit signs you in. Read the email on any device.</p>
      )}
      {spent ? (
        <div className="onboarding-link-row onboarding-new-code-row">
          <button type="button" className="button-primary" disabled={email.busy} onClick={sendCode}>Send a new code</button>
          <LinkRow links={[['Change email', onChangeEmail], ['Use the browser instead', onBrowser]]} />
        </div>
      ) : (
        <LinkRow
          links={[['Send a new code', sendCode], ['Change email', onChangeEmail], ['Use the browser instead', onBrowser]]}
          cancel={() => onAction('onboarding_sign_in_cancel')}
        />
      )}
    </>
  );
}

// The email door's choice, before anything is sent: an emailed code, which
// also makes the account of an address that has none, or the password.
function ChooseStep({ email, onAction, onChangeEmail }: { email: EmailSignIn; onAction: Action; onChangeEmail: () => void }) {
  const [password, setPassword] = useState('');
  const refusal = refusalText(email);
  const codeRefused = email.refusal === 'too_many_codes';
  return (
    <>
      <p className="onboarding-door-title">How do you want to sign in?</p>
      <p className="onboarding-door-detail">
        {email.email}.{' '}
        <button type="button" className="onboarding-link inline" onClick={onChangeEmail}>Change email</button>
      </p>
      <button type="button" className="button-primary onboarding-code-button" disabled={email.busy}
        onClick={() => onAction('onboarding_sign_in_send_code')}>
        Email me a 6-digit code
      </button>
      {codeRefused ? (
        <p className="onboarding-error" role="alert">{refusal}</p>
      ) : (
        <p className="onboarding-door-note">New here? The code makes your account.</p>
      )}
      <p className="onboarding-or"><span>or</span></p>
      <form className="onboarding-email-row" onSubmit={(event) => {
        event.preventDefault();
        if (password) onAction('onboarding_sign_in_password', { password });
      }}>
        <input className="onboarding-field" type="password" placeholder="Password" autoComplete="current-password"
          aria-label="Password" value={password} disabled={email.busy} onChange={(event) => setPassword(event.target.value)} />
        <button type="submit" className="button-secondary" disabled={email.busy || !password}>Sign in</button>
      </form>
      {refusal && !codeRefused && <p className="onboarding-error" role="alert">{refusal}</p>}
      <LinkRow
        links={[['Forgot password', () => onAction('onboarding_open_link', { link: 'reset_password' })]]}
        cancel={() => onAction('onboarding_sign_in_cancel')}
      />
    </>
  );
}

// The first door, on the welcome and in Home's Agent group alike: the
// first-month offer, Google in the browser or an email that stays in the
// app, each step of either, and who signed in. Returns the words and
// controls only; the caller frames them.
export function SignInDoor({ onboarding, onAction, title }: { onboarding: OnboardingState; onAction: Action; title?: string }) {
  // The address typed at rest, kept for "Change email".
  const [address, setAddress] = useState('');
  const addressField = useRef<HTMLInputElement>(null);
  const [focusAddress, setFocusAddress] = useState(false);
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
  if (onboarding.signIn === 'email' && email)
    return <ChooseStep email={email} onAction={onAction} onChangeEmail={backToAddress} />;
  if (onboarding.signIn === 'browser') {
    return (
      <>
        <p className="onboarding-door-title onboarding-waiting">
          <span className="onboarding-spinner" aria-hidden="true" />
          Finish signing in in your browser.
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
  if (onboarding.accountEmail) {
    const total = onboarding.account?.credits.total;
    return (
      <p className="onboarding-door-title" role="status">
        Signed in as {onboarding.accountEmail}
        {total !== undefined && ` · ${creditCount(total)}`}
      </p>
    );
  }
  // At rest.
  return (
    <>
      {title && <p className="onboarding-door-title">{title}</p>}
      {title && <p className="onboarding-door-detail">An Expert message is 5 credits. Your own API key stays free.</p>}
      {title && onboarding.v1Imported && (
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
        const typed = address.trim();
        if (typed) onAction('onboarding_sign_in_email', { email: typed });
      }}>
        <input ref={addressField} className="onboarding-field" type="email" placeholder="you@example.com" autoComplete="email"
          aria-label="Email" value={address} onChange={(event) => setAddress(event.target.value)} />
        <button type="submit" className="button-primary" disabled={!address.trim()}>Continue with email</button>
      </form>
      <p className="onboarding-door-note">New or existing account, same door. Google opens your browser; email stays here.</p>
      <p className="onboarding-door-note">
        By signing in you agree to the{' '}
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

// The two free doors: each opens Preferences › Agent, and closes back here.
export function FreeDoors({ onAction, disabled }: { onAction: Action; disabled: boolean }) {
  const door = (which: 'key' | 'tool', name: string, cost: string) => (
    <button type="button" className="onboarding-door" disabled={disabled}
      onClick={() => onAction('onboarding_open_preferences', { door: which })}>
      <span className="onboarding-door-name">{name}</span>
      <span className="onboarding-door-cost">{cost}</span>
    </button>
  );
  return (
    <>
      {door('key', 'Your own API key', 'Free')}
      {door('tool', 'Claude Code or Claude Desktop', 'Free · MCP server')}
    </>
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
          <video src={clip} autoPlay loop muted playsInline aria-label="The agent fixing a print that came out stringy" />
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
              Show it a photo of what went wrong. The agent reads your file, your settings and your slice, changes the settings
              and slices again.
            </p>
          </div>
          <div className="onboarding-doors">
            <p className="onboarding-doors-label">Pick how the agent should think. Change it any time in Preferences › Agent.</p>
            <div className="onboarding-offer">
              <SignInDoor onboarding={onboarding} onAction={onAction} title="Your first month: 500 credits" />
              {signedIn && !waited.current && (
                <button type="button" className="button-primary onboarding-door-button"
                  onClick={() => onAction('onboarding_leave_welcome')}>
                  Continue
                </button>
              )}
            </div>
            <FreeDoors onAction={onAction} disabled={checking || (signedIn && waited.current)} />
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

function OrcaImport({ onboarding, error, onAction }: Props) {
  const { orca } = onboarding;
  return (
    <div className="onboarding-step">
      <div className="onboarding-heading">
        <h1>Bring your printers from OrcaSlicer?</h1>
        <p className="onboarding-lead">
          JusPrin copies these printers with their filaments, settings and printer connections. OrcaSlicer keeps its own copy.
        </p>
      </div>
      <ul className="onboarding-printers">
        {orca.printers.map((name) => (
          <li key={name}>
            <PrinterGlyph />
            {name}
          </li>
        ))}
      </ul>
      <p className="onboarding-source">
        <FolderGlyph />
        <span className="onboarding-source-path">{orca.source}</span>
        {orca.filamentCount > 0 && <span>· {orca.filamentCount === 1 ? '1 filament' : `${orca.filamentCount} filaments`}</span>}
      </p>
      {error && <p className="onboarding-error" role="alert">{error}</p>}
      <div className="onboarding-actions">
        <button type="button" className="button-primary" onClick={() => onAction('onboarding_import')}>Import</button>
        <button type="button" className="button-secondary" onClick={() => onAction('onboarding_start_fresh')}>Start fresh</button>
      </div>
      <button type="button" className="onboarding-link flush" onClick={() => onAction('onboarding_back')}>Back</button>
    </div>
  );
}

function importStatus(item: ImportResultItem) {
  if (!item.imported) return `Not imported · ${item.reason}`;
  return item.renamedTo ? `Imported as ‘${item.renamedTo}’` : 'Imported';
}

function ImportResult({ onboarding, error, onAction }: Props) {
  const items = onboarding.orca.importResult ?? [];
  const failed = items.some((item) => !item.imported);
  // Printers and processes a line each; filaments as a count, with any not
  // imported listed so each says why.
  const filaments = items.filter((item) => item.kind === 'filament');
  const importedFilaments = filaments.filter((item) => item.imported).length;
  const listed = items.filter((item) => item.kind !== 'filament' || !item.imported);
  const noun = filaments.length === 1 ? 'filament' : 'filaments';
  return (
    <div className="onboarding-step">
      <div className="onboarding-heading">
        <h1>{failed ? 'Some settings need attention' : 'Imported from OrcaSlicer'}</h1>
        <p className="onboarding-lead">OrcaSlicer’s own files were not changed.</p>
      </div>
      {listed.length > 0 && (
        <ul className="onboarding-printers">
          {listed.map((item, index) => (
            <li key={`${item.kind}:${item.name}:${index}`}>
              {item.imported ? <CheckGlyph /> : <WarningGlyph />}
              <span className="onboarding-import-line">
                <span>{item.name}</span>
                <span className="onboarding-import-status">{importStatus(item)}</span>
              </span>
            </li>
          ))}
        </ul>
      )}
      {filaments.length > 0 && (
        <p className="onboarding-import-count">
          {importedFilaments === filaments.length
            ? `${filaments.length} ${noun} imported`
            : `${importedFilaments} of ${filaments.length} ${noun} imported`}
        </p>
      )}
      {error && <p className="onboarding-error" role="alert">{error}</p>}
      <div className="onboarding-actions">
        <button type="button" className="button-primary" onClick={() => onAction('onboarding_import_continue')}>Continue</button>
        {failed && (
          <button type="button" className="button-secondary" onClick={() => onAction('onboarding_import_retry')}>
            Retry failed items
          </button>
        )}
      </div>
    </div>
  );
}

function AddPrinter({ onboarding, error, onAction }: Props) {
  const form = onboarding.printerForm;
  const back = () => onAction('onboarding_back');
  return (
    <div className="onboarding-step onboarding-add-printer">
      {form ? (
        <PrinterFormHost
          form={form}
          onAsk={(catalogId, nozzle) => onAction('onboarding_printer_form', { action: 'form_options', catalogId, nozzle })}
          onFinish={(choice) => onAction('onboarding_printer_form', { action: 'form_finish', ...choice })}
          // The form's own Cancel and Escape are the step's Back.
          onClose={back}
        />
      ) : (
        <p className="onboarding-lead" role="status">Reading the printer list…</p>
      )}
      {error && <p className="onboarding-error" role="alert">{error}</p>}
      {!onboarding.orca.found && (
        <button type="button" className="onboarding-link flush" onClick={() => onAction('onboarding_choose_orca_folder')}>
          Import from an OrcaSlicer folder…
        </button>
      )}
      <div className="onboarding-nav">
        <button type="button" className="onboarding-link flush" onClick={back}>Back</button>
        <button type="button" className="onboarding-link flush" onClick={() => onAction('onboarding_assume_default')}>
          Skip, use a default
        </button>
      </div>
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
          : <AddPrinter {...props} />}
    </div>
  );
}
