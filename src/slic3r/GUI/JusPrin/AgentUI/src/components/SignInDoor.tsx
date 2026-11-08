// The JusPrin account's sign-in, drawn once for every screen that signs
// someone in: the first-launch welcome and Home's AI group (the Home page),
// and Preferences › Account (the Agent page). Canvas
// "JusPrin v2", boards first-run-01, 04 and 11 to 16: Google in the browser,
// or an email that stays in the app, then its emailed code or the password.
// The server's own sign-in page is never shown.
//
// Each page speaks its own bridge, so the caller hands over where the sign-in
// has got to and what each control does; this file only draws them. It
// returns the words and controls only: the caller frames them in a column.

import { ReactNode, useEffect, useRef, useState } from 'react';
import googleMark from '@resources/jusprin/welcome/google-g.svg';
import { CodeField } from './CodeField';
import './SignInDoor.css';

export type SignInPhase = 'idle' | 'browser' | 'email' | 'checking';

// The server's refusal of the last code, password or request for a code.
export type SignInRefusal = '' | 'wrong_code' | 'expired_code' | 'too_many_tries' | 'too_many_codes' | 'wrong_password';

// The email sign-in while the phase is 'email': the person picks an emailed
// code or the password ('choose'), or the app waits for the code ('code').
// 'expired_code' is a code that no longer works: it expired or was tried too
// often.
export interface SignInEmailStep {
  step: 'choose' | 'code';
  email: string;
  // A code, a password or a request for a code is with the server.
  busy: boolean;
  refusal: SignInRefusal;
  // A new code went out because the person asked for one.
  codeResent: boolean;
}

export type SignInLink = 'terms' | 'privacy' | 'reset_password';

export interface SignInActions {
  google: () => void;
  // The address typed; nothing is sent yet.
  email: (address: string) => void;
  sendCode: () => void;
  code: (digits: string) => void;
  password: (password: string) => void;
  cancel: () => void;
  // Google's page again, for a browser that did not open.
  reopen: () => void;
  openLink: (link: SignInLink) => void;
}

// The page's own button classes: the Home page and the Agent page each draw
// the design system's buttons with their own.
export interface SignInButtons {
  primary: string;
  secondary: string;
}

export interface SignInDoorProps {
  phase: SignInPhase;
  email: SignInEmailStep | null | undefined;
  // Why the last sign-in did not finish, already worded.
  error?: string;
  actions: SignInActions;
  buttons: SignInButtons;
  // What the screen says above the door while nothing has started, such as
  // the sign-up offer.
  children?: ReactNode;
}

// What the server's refusal means to the person.
function refusalText(email: SignInEmailStep): string {
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
    <div className="sign-in-link-row">
      <span className="sign-in-links">
        {links.map(([label, onClick], index) => (
          <span key={label}>
            {index > 0 && <span className="sign-in-link-dot" aria-hidden="true">·</span>}
            <button type="button" className="sign-in-link" onClick={onClick}>{label}</button>
          </span>
        ))}
      </span>
      {cancel && <button type="button" className="sign-in-link" onClick={cancel}>Cancel</button>}
    </div>
  );
}

// The code step (boards first-run-14 to 16).
function CodeStep({ email, actions, buttons, onChangeEmail, onBrowser }: {
  email: SignInEmailStep; actions: SignInActions; buttons: SignInButtons; onChangeEmail: () => void; onBrowser: () => void;
}) {
  const [code, setCode] = useState('');
  const field = useRef<HTMLInputElement>(null);
  const refusal = refusalText(email);
  // A code that can no longer work: the way on is a new one.
  const spent = email.refusal === 'expired_code';
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
      <p className="sign-in-title">Check your email.</p>
      <p className="sign-in-detail">
        {email.codeResent ? 'We sent a new code to ' : 'We sent a 6-digit code to '}
        {email.email}.
      </p>
      <CodeField
        ref={field}
        value={code}
        refused={email.refusal === 'wrong_code' || spent}
        disabled={email.busy}
        onChange={setCode}
        onComplete={(digits) => actions.code(digits)}
      />
      {refusal ? (
        <p className="sign-in-error" role="alert">{refusal}</p>
      ) : (
        <p className="sign-in-note">Type the code from the email. You can open it on your phone.</p>
      )}
      {spent ? (
        <div className="sign-in-link-row sign-in-new-code-row">
          <button type="button" className={buttons.primary} disabled={email.busy} onClick={actions.sendCode}>Send a new code</button>
          <LinkRow links={[['Change email', onChangeEmail], ['Sign in with Google instead', onBrowser]]} />
        </div>
      ) : (
        <LinkRow
          links={[['Send a new code', actions.sendCode], ['Change email', onChangeEmail], ['Sign in with Google instead', onBrowser]]}
          cancel={actions.cancel}
        />
      )}
    </>
  );
}

// Before anything is sent: an emailed code, which also makes the account of
// an address that has none, and, for someone who set one, the password
// behind "Use a password instead".
function ChooseStep({ email, actions, buttons, onChangeEmail }: {
  email: SignInEmailStep; actions: SignInActions; buttons: SignInButtons; onChangeEmail: () => void;
}) {
  const [password, setPassword] = useState('');
  const [withPassword, setWithPassword] = useState(email.refusal === 'wrong_password');
  const refusal = refusalText(email);
  const codeRefused = email.refusal === 'too_many_codes';
  return (
    <>
      <p className="sign-in-title">We’ll email you a code.</p>
      <p className="sign-in-detail">
        {email.email}.{' '}
        <button type="button" className="sign-in-link inline" onClick={onChangeEmail}>Change email</button>
      </p>
      <button type="button" className={`${buttons.primary} sign-in-code-button`} disabled={email.busy}
        onClick={actions.sendCode}>
        Email me a 6-digit code
      </button>
      {codeRefused && <p className="sign-in-error" role="alert">{refusal}</p>}
      {withPassword ? (
        <>
          <p className="sign-in-or"><span>or</span></p>
          <form className="sign-in-email-row" onSubmit={(event) => {
            event.preventDefault();
            if (password) actions.password(password);
          }}>
            <input className="sign-in-field" type="password" placeholder="Password" autoComplete="current-password"
              aria-label="Password" value={password} disabled={email.busy} autoFocus
              onChange={(event) => setPassword(event.target.value)} />
            <button type="submit" className={buttons.secondary} disabled={email.busy || !password}>Sign in</button>
          </form>
          {refusal && !codeRefused && <p className="sign-in-error" role="alert">{refusal}</p>}
          <LinkRow links={[['Forgot password', () => actions.openLink('reset_password')]]} cancel={actions.cancel} />
        </>
      ) : (
        <LinkRow links={[['Use a password instead', () => setWithPassword(true)]]} cancel={actions.cancel} />
      )}
    </>
  );
}

// Signing up and signing in are the same door: an address with no account
// gets one from its first code, so "Sign up free" sends it at once, and only
// "Sign in" asks how (board first-run-11). Who signed in is the caller's to
// say: this draws signing in only.
export function SignInDoor({ phase, email, error, actions, buttons, children }: SignInDoorProps) {
  // The address typed at rest, kept for "Change email".
  const [address, setAddress] = useState('');
  const addressField = useRef<HTMLInputElement>(null);
  const [focusAddress, setFocusAddress] = useState(false);
  // "Sign up free" was pressed: the code is on its way, so the step that asks
  // how to sign in is not shown while the server sends it.
  const [sendingCode, setSendingCode] = useState(false);
  useEffect(() => {
    if (focusAddress && phase === 'idle') {
      addressField.current?.focus();
      setFocusAddress(false);
    }
  }, [focusAddress, phase]);
  const backToAddress = () => {
    actions.cancel();
    setFocusAddress(true);
  };
  const browser = () => {
    actions.cancel();
    actions.google();
  };

  if (phase === 'email' && email && email.step === 'code')
    return <CodeStep email={email} actions={actions} buttons={buttons} onChangeEmail={backToAddress} onBrowser={browser} />;
  if (phase === 'email' && email && sendingCode && !email.refusal) {
    return (
      <>
        <p className="sign-in-title sign-in-waiting">
          <span className="sign-in-spinner" aria-hidden="true" />
          Sending a code to {email.email}…
        </p>
        <LinkRow links={[]} cancel={actions.cancel} />
      </>
    );
  }
  if (phase === 'email' && email)
    return <ChooseStep email={email} actions={actions} buttons={buttons} onChangeEmail={backToAddress} />;
  if (phase === 'browser') {
    return (
      <>
        <p className="sign-in-title sign-in-waiting">
          <span className="sign-in-spinner" aria-hidden="true" />
          Finish in your browser.
        </p>
        <p className="sign-in-detail">The app continues on its own once the browser is done.</p>
        <p className="sign-in-note">
          Nothing happened?{' '}
          <button type="button" className="sign-in-link inline" onClick={actions.reopen}>Open the browser again</button>
        </p>
        <LinkRow links={[['Use an email code instead', backToAddress]]} cancel={actions.cancel} />
      </>
    );
  }
  if (phase === 'checking') {
    return (
      <>
        <p className="sign-in-title sign-in-waiting">
          <span className="sign-in-spinner" aria-hidden="true" />
          Signing you in…
        </p>
        <LinkRow links={[]} cancel={actions.cancel} />
      </>
    );
  }
  // At rest. The host takes the address, then the code request, in order.
  const sendEmail = (withCode: boolean) => {
    const typed = address.trim();
    if (!typed) {
      addressField.current?.focus();
      return;
    }
    setSendingCode(withCode);
    actions.email(typed);
    if (withCode) actions.sendCode();
  };
  return (
    <>
      {children}
      <button type="button" className={`${buttons.secondary} sign-in-google`} onClick={actions.google}>
        <img src={googleMark} alt="" />
        Continue with Google
      </button>
      <p className="sign-in-or"><span>or</span></p>
      <form className="sign-in-email-row" onSubmit={(event) => {
        event.preventDefault();
        sendEmail(true);
      }}>
        <input ref={addressField} className="sign-in-field" type="email" placeholder="you@example.com" autoComplete="email"
          aria-label="Email" value={address} onChange={(event) => setAddress(event.target.value)} />
        <button type="submit" className={buttons.primary} disabled={!address.trim()}>Sign up free</button>
      </form>
      <p className="sign-in-note">
        Already have an account?{' '}
        <button type="button" className="sign-in-link inline" onClick={() => sendEmail(false)}>Sign in</button>
      </p>
      <p className="sign-in-note">
        By continuing you agree to the{' '}
        <button type="button" className="sign-in-link inline" onClick={() => actions.openLink('terms')}>Terms</button>{' '}
        and{' '}
        <button type="button" className="sign-in-link inline" onClick={() => actions.openLink('privacy')}>Privacy Policy</button>
        .
      </p>
      {error && <p className="sign-in-error" role="alert">{error}</p>}
    </>
  );
}
