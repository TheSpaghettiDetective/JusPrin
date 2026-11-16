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
import type { ReactNode } from 'react';
import { plural, t } from '@lingui/core/macro';
import { Trans } from '@lingui/react/macro';
import type { Appearance, OnboardingState } from '../bridge/protocol';
import { afterPaint } from '@shared/afterPaint';
import { SIGNUP_AFTER, SIGNUP_OFFER, SIGNUP_SCALE, signedInCredits } from '../../../AgentUI/src/credits';
import { OrcaImportResultLines, OrcaPrinterList } from '../../../AgentUI/src/components/OrcaImportLists';
import clip from '@resources/jusprin/welcome/welcome-clip.mp4';
import clipWebm from '@resources/jusprin/welcome/welcome-clip.webm';
import { SignInDoor as SharedSignInDoor, SignInActions } from '../../../AgentUI/src/components/SignInDoor';
import { CheckGlyph, FolderGlyph } from './Glyphs';
import { Lockup } from './Lockup';
import { PrinterPickerHost } from './PrinterPickerHost';
import type { PrinterPick } from '../../../AgentUI/src/components/PrinterPicker';

// What Skip installs: OrcaSlicer's Generic Klipper Printer, its 250 × 250 mm
// bed and Klipper G-code (OrcaHomeBackend::assume_default_printer). Home's
// "No printer yet" card and its drop zone say the same.
export const STAND_IN_PRINTER = t`Until you add yours, files are sliced for a 250 mm Klipper printer and may not print on other printers.`;
// The same before Skip, under the step's button and Skip for now.
const SKIP_PRINTER = t`If you skip, files are sliced for a stand-in printer with a 250 mm bed, not for yours. Add your printer before you print anything. You can do that later on the home screen.`;

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
  const email = onboarding.accountEmail;
  if (!account) return <p className="onboarding-door-title" role="status"><Trans>Signed in as {email}</Trans></p>;
  return (
    <div role="status">
      <p className="onboarding-door-title">{signedInCredits(account)}</p>
      <p className="onboarding-door-detail"><Trans>Signed in as {email}</Trans></p>
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
          <button type="button" className="sign-in-link inline" onClick={() => onAction('onboarding_see_prices')}>
            <Trans>See prices</Trans>
          </button>
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
        <span className="onboarding-door-name"><Trans>Use your own AI account</Trans></span>
        <span className="onboarding-door-what"><Trans>Your API key, or Claude Code or Claude Desktop</Trans></span>
      </span>
      <span className="onboarding-door-cost">{t({ message: 'Free', comment: 'What a door costs: nothing' })}</span>
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
  const author = '#3DBenchy';
  const license = 'CC BY 2.0 (creativecommons.org/licenses/by/2.0)';
  const firstTitle = 'Dual extrusion 3D printed #3DBenchy v01';
  const firstSource = 'flickr.com/photos/3dbenchy/19273965123';
  const secondTitle = 'Dual-print multi-part color 3D print of #3DBenchy v13';
  const secondSource = 'flickr.com/photos/3dbenchy/19239738473';

  return (
    <div className="onboarding">
      <Lockup appearance={appearance} className="onboarding-lockup" />
      <div className="onboarding-welcome">
        <figure className="onboarding-clip">
          {/* WebM first: Linux's web view plays it with the codecs every distro ships, but
              H.264 only once the user installs extra ones. */}
          <video autoPlay loop muted playsInline aria-label={t`AI fixing a print that came out stringy`}>
            <source src={clipWebm} type="video/webm" />
            <source src={clip} type="video/mp4" />
          </video>
          <details className="onboarding-credits">
            <summary><Trans>Photo credits</Trans></summary>
            {/* The titles, author, links and licence are the photos' own, kept as they are. */}
            <p>
              <Trans comment="A photo's attribution: the title, author, link and licence are the photo's own">
                “{firstTitle}” by {author}, {firstSource}, {license}. Cropped.
              </Trans>
            </p>
            <p>
              <Trans comment="A photo's attribution: the title, author, link and licence are the photo's own">
                “{secondTitle}” by {author}, {secondSource}, {license}. Cropped and resized.
              </Trans>
            </p>
          </details>
        </figure>
        <div className="onboarding-pitch">
          <div className="onboarding-heading">
            <h1><Trans>The AI slicer that helps you get better prints.</Trans></h1>
            <p className="onboarding-lead">
              <Trans>
                JusPrin’s AI specializes in 3D printing. It sets up your prints, explains your settings and fixes what goes
                wrong.
              </Trans>
            </p>
          </div>
          {/* Once signed in there is nothing left to choose: only the credits
              card, with Continue, stays. */}
          <div className="onboarding-doors">
            {!signedIn && <p className="onboarding-doors-label"><Trans>Sign up to use AI, or skip it and just slice.</Trans></p>}
            <div className="onboarding-offer">
              <SignInDoor onboarding={onboarding} onAction={onAction} offer />
              {signedIn && !waited.current && (
                <button type="button" className="button-primary onboarding-door-button"
                  onClick={() => onAction('onboarding_leave_welcome')}>
                  <Trans>Continue</Trans>
                </button>
              )}
            </div>
            {!signedIn && (
              <>
                <OwnAccountDoor onAction={onAction} disabled={checking} />
                <button type="button" className="onboarding-link flush" disabled={checking}
                  onClick={() => onAction('onboarding_leave_welcome')}>
                  <Trans>Just slice for now</Trans>
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
  const { account, accountEmail: email } = onboarding;
  if (email && account) {
    return (
      <div className="onboarding-done">
        <CheckGlyph />
        <span className="onboarding-done-text">
          <span>{signedInCredits(account)}</span>
          <span className="onboarding-done-detail"><Trans>Signed in as {email}</Trans></span>
        </span>
      </div>
    );
  }
  const provider = onboarding.agentKeyProvider;
  const text = email ? t`Signed in as ${email}`
    : provider ? t({ message: `AI uses your ${provider} key`, comment: '{provider} is the AI company, such as Anthropic or OpenAI' }) : '';
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
      <span className="onboarding-ways-label"><Trans>Other ways:</Trans></span>
      {ways.map((way) => {
        const app = way === 'printer' ? '' : IMPORT_APPS[way];
        return (
          <button key={way} type="button" className="onboarding-link" disabled={disabled}
            onClick={() => onAction('onboarding_way', { way })}>
            {way === 'printer' ? t`Choose my printer` : t`Import from ${app}…`}
          </button>
        );
      })}
    </div>
  );
}

// Why the last way's folder did not open its screen.
function WayProblem({ onboarding }: { onboarding: OnboardingState }) {
  return onboarding.wayProblem ? <p className="onboarding-error" role="alert">{onboarding.wayProblem}</p> : null;
}

// "Also brings 3 filaments and 2 print settings.", or either alone.
function alsoBrings(filaments: number, settings: number): string {
  if (filaments > 0 && settings > 0) {
    return t`Also brings ${plural(filaments, { one: `${filaments} filament`, other: `${filaments} filaments` })} and ${plural(settings, { one: `${settings} print setting`, other: `${settings} print settings` })}.`;
  }
  if (filaments > 0) return plural(filaments, { one: `Also brings ${filaments} filament.`, other: `Also brings ${filaments} filaments.` });
  if (settings > 0) return plural(settings, { one: `Also brings ${settings} print setting.`, other: `Also brings ${settings} print settings.` });
  return '';
}

// The step's own button with Skip for now beside it, and what skipping does
// under them: the same on all three printer screens.
function SkipStep({ children, note, disabled, onAction }: { children: ReactNode; note: boolean; disabled?: boolean; onAction: Action }) {
  return (
    <div className="onboarding-step-group">
      <div className="onboarding-actions">
        {children}
        <button type="button" className="onboarding-link" disabled={disabled} onClick={() => onAction('onboarding_assume_default')}><Trans>Skip for now</Trans></button>
      </div>
      {note && <p className="onboarding-note">{SKIP_PRINTER}</p>}
    </div>
  );
}

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
  const brings = alsoBrings(found.filamentCount, found.settingsCount);
  const source = found.source;
  return (
    <div className="onboarding-step">
      <WelcomeDone onboarding={onboarding} />
      <div className="onboarding-heading">
        <h1><Trans>Bring your printers from {app}?</Trans></h1>
        <p className="onboarding-lead">
          <Trans>JusPrin copies these printers with their filaments and print settings. {app} keeps its own copy.</Trans>
        </p>
      </div>
      <OrcaPrinterList app={app} printers={found.printers.map((name) => ({ name, already: false }))} chosen={chosen}
        onChoose={choose} disabled={busy} />
      <div className="onboarding-source">
        {brings && <p>{brings}</p>}
        <p className="onboarding-source-folder">
          <FolderGlyph />
          <span className="onboarding-source-path"><Trans>{app} keeps these in {source}</Trans></span>
        </p>
      </div>
      {error && <p className="onboarding-error" role="alert">{error}</p>}
      {/* Every printer starts ticked, so the note stays: nothing here says
          the person meant to import. */}
      <SkipStep note disabled={busy} onAction={onAction}>
        <button type="button" className="button-primary" disabled={busy || chosen.length === 0}
          onClick={() => run('onboarding_import', { app: id, printers: chosen })}>
          {busy ? t`Importing…` : t`Import`}
        </button>
      </SkipStep>
      <WayProblem onboarding={onboarding} />
      <OtherWays ways={[id === 'jusprin1' ? 'orcaslicer' : 'jusprin1', 'printer']} disabled={busy} onAction={onAction} />
      <button type="button" className="onboarding-link flush" disabled={busy} onClick={() => onAction('onboarding_back')}>
        {t({ message: 'Back', context: 'navigation' })}
      </button>
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
        <h1><Trans>Imported from {app}</Trans></h1>
      </div>
      <OrcaImportResultLines app={app} lines={items} />
      {error && <p className="onboarding-error" role="alert">{error}</p>}
      <div className="onboarding-actions">
        <button type="button" className="button-primary" onClick={() => onAction('onboarding_import_continue')}>
          <Trans>Continue</Trans>
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
        <h1><Trans>Which printer do you have?</Trans></h1>
        <p className="onboarding-lead">
          <Trans>JusPrin prepares every print to fit your printer’s bed and nozzle. You can add more printers later.</Trans>
        </p>
      </div>
      {form ? (
        <PrinterPickerHost form={form} pick={pick} onPick={setPick}
          reading={Boolean(form.loading && catalogId && nozzle !== undefined)}
          onAskAgent={agentReady ? () => onAction('onboarding_ask_agent') : undefined} />
      ) : (
        <p className="onboarding-lead" role="status"><Trans>Reading the printer list…</Trans></p>
      )}
      {options?.error && <p className="onboarding-error" role="alert">{options.error}</p>}
      {form?.error && <p className="onboarding-error" role="alert">{form.error}</p>}
      {error && <p className="onboarding-error" role="alert">{error}</p>}
      <SkipStep note={!catalogId} onAction={onAction}>
        <button type="button" className="button-primary" disabled={!ready || finishing} onClick={finish}><Trans>Continue</Trans></button>
      </SkipStep>
      <WayProblem onboarding={onboarding} />
      <OtherWays ways={['jusprin1', 'orcaslicer']} onAction={onAction} />
      <button type="button" className="onboarding-link flush" onClick={() => onAction('onboarding_back')}>
        {t({ message: 'Back', context: 'navigation' })}
      </button>
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
