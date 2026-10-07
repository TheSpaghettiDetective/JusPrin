// First-run onboarding, local path only. Every screen is the Figma frame of
// the same name on the "On boarding experience" page; the host decides which
// one is due (Home/OnboardingModel) and this file only draws it.
//
// The frames carry illustrative data ("Example local source", "2 example
// items", a sample preset list). Nothing here repeats it: a row is drawn only
// from what the host reported, and a line the host cannot back is left out.

import { useState } from 'react';
import type { OnboardingState } from '../bridge/protocol';

// No account backend exists yet. Anything that needs one is a link to this
// placeholder, which the host opens in the browser.
const ACCOUNT_STUB = 'https://jusprin.com/account';
const ACCOUNT_STUB_NOTE = 'Account setup is not available in this build. Opens jusprin.com.';

type Category = 'printer' | 'filament' | 'process';
type ProjectAction = 'import_project' | 'onboarding_open_example';

interface Props {
  onboarding: OnboardingState;
  error?: string;
  onAction: (type: string, payload?: object) => void;
  onDismissError: () => void;
}

type ActionProps = Pick<Props, 'onAction'>;

function Glyph({ name }: { name: string }) {
  return <span className={`glyph glyph-${name}`} aria-hidden="true" />;
}

function statusText(onboarding: OnboardingState): string {
  if (onboarding.agentConfigured) return 'Local mode · Agent configured';
  return onboarding.step === 'welcome' ? 'Agent not connected' : 'Local mode · Agent not configured';
}

function Header({ onboarding }: Pick<Props, 'onboarding'>) {
  const waiting = onboarding.step === 'welcome' && !onboarding.agentConfigured;
  return (
    <header className="onboarding-header">
      <div className="onboarding-identity">
        <span className="onboarding-wordmark">JusPrin</span>
        <Glyph name="chevron-right" />
        <span>Agent</span>
      </div>
      <div className="onboarding-status">
        <span className={`onboarding-status-dot${waiting ? ' waiting' : ''}`} aria-hidden="true" />
        {statusText(onboarding)}
      </div>
    </header>
  );
}

function footerText(onboarding: OnboardingState): string {
  const { step, profiles } = onboarding;
  if (step === 'welcome') {
    return 'Local setup and projects are available without an Agent. Account setup comes first if you want to connect one.';
  }
  if (step === 'profiles') {
    if (profiles.partialImportPending) {
      return 'Continue with the settings that imported successfully. We’ll show only the local setup steps still needed.';
    }
    return profiles.available
      ? 'Import is optional. Continue with local setup without Agent.'
      : 'Preset import is optional. Local migration uses fixed rules and does not depend on AI.';
  }
  if (step === 'confirm_setup') return 'Imported presets describe a configuration, not a discovered or connected printer.';
  if (step === 'project') return 'No project is open yet. Choose a file or the example to continue to Prepare.';
  return '';
}

function Heading({ eyebrow, title, children }: { eyebrow: string; title: string; children?: React.ReactNode }) {
  return (
    <div className="onboarding-heading">
      <p className="onboarding-eyebrow">{eyebrow}</p>
      <h1>{title}</h1>
      {children && <p className="onboarding-copy">{children}</p>}
    </div>
  );
}

// `flush` lines the label up with the text column, `block` makes the whole
// row the target, and `text` is a bare line with no control height.
function LinkButton({ children, variant, onClick }: {
  children: React.ReactNode;
  variant?: 'flush' | 'block' | 'text';
  onClick: () => void;
}) {
  return (
    <button type="button" className={variant ? `onboarding-link ${variant}` : 'onboarding-link'} onClick={onClick}>
      {children}
    </button>
  );
}

// A failure on a step that has no recovery frame of its own is said in place,
// above the actions that caused it.
function ActionError({ error }: { error?: string }) {
  return error ? <p className="onboarding-error" role="alert">{error}</p> : null;
}

function Welcome({ onAction }: ActionProps) {
  return (
    <div className="onboarding-welcome">
      <section className="onboarding-intro">
        <div className="onboarding-heading onboarding-welcome-heading">
          <p className="onboarding-eyebrow">Welcome to JusPrin</p>
          <h1>Your Orca workflow, with help planning the print.</h1>
          <p className="onboarding-lead">
            Bring along your existing projects and tuned presets. Describe what matters, review proposed changes, and keep
            control—with Undo whenever you need it.
          </p>
        </div>
        <div className="onboarding-routes">
          <div className="onboarding-route">
            <a
              href={ACCOUNT_STUB}
              className="button-primary"
              title={ACCOUNT_STUB_NOTE}
              onClick={(event) => {
                event.preventDefault();
                onAction('onboarding_account_stub');
              }}
            >
              Set up JusPrin
            </a>
            <p>Start with account setup.<br />Connect an Agent afterward.</p>
          </div>
          <div className="onboarding-route">
            <button type="button" className="button-secondary" onClick={() => onAction('onboarding_begin')}>
              Explore first
            </button>
            <p>Start locally with profiles, printer setup, then a project. Steps you’ve already completed are skipped.</p>
          </div>
        </div>
        <div className="onboarding-dismissal">
          <LinkButton variant="text" onClick={() => onAction('onboarding_dismiss')}>Dismiss onboarding and continue locally</LinkButton>
          <p>Leave this guide and use JusPrin locally. You can open setup later.</p>
        </div>
      </section>
      <section className="onboarding-demo" aria-label="Example interaction">
        <div className="onboarding-demo-disclosure">
          <p className="onboarding-eyebrow">Example interaction · Not live</p>
          <p>Illustrative only. No project has been inspected.</p>
        </div>
        <div className="onboarding-conversation">
          <div className="onboarding-message">
            <p className="onboarding-speaker">You</p>
            <p>Keep the front smooth. I don’t mind a longer print.</p>
          </div>
          <div className="onboarding-message">
            <p className="onboarding-speaker">Agent · example response</p>
            <p>I’ll compare orientations that keep support contact off the front, then show you the tradeoff.</p>
          </div>
        </div>
        <dl className="onboarding-summary">
          <div><dt>Your intent</dt><dd className="strong">Better surface finish</dd></div>
          <div><dt>Your control</dt><dd>Inspect · Edit · Undo</dd></div>
        </dl>
      </section>
    </div>
  );
}

function SourceCard({ source, compact }: { source: string; compact?: boolean }) {
  return (
    <div className={`onboarding-source${compact ? ' compact' : ''}`}>
      <div className="onboarding-source-name"><Glyph name="monitor" />OrcaSlicer local data</div>
      <p className="onboarding-source-path">{source}</p>
      <p className="onboarding-source-scope">Detected on this computer</p>
    </div>
  );
}

const CATEGORIES: { key: Category; label: string; directory: string; kind: string }[] = [
  { key: 'printer', label: 'Printer presets', directory: 'machine', kind: 'Printer preset' },
  { key: 'filament', label: 'Filament presets', directory: 'filament', kind: 'Filament preset' },
  { key: 'process', label: 'Process presets', directory: 'process', kind: 'Process preset' },
];

function OrcaFound({ onboarding, error, onAction }: Pick<Props, 'onboarding' | 'error' | 'onAction'>) {
  const { profiles } = onboarding;
  const [checked, setChecked] = useState<Record<Category, boolean>>({ printer: true, filament: true, process: true });
  const [reviewing, setReviewing] = useState(false);
  const counts: Record<Category, number> = {
    printer: profiles.printerCount,
    filament: profiles.filamentCount,
    process: profiles.processCount,
  };
  const names: Record<Category, string[]> = {
    printer: profiles.printerNames ?? [],
    filament: profiles.filamentNames ?? [],
    process: profiles.processNames ?? [],
  };
  // A category with nothing in it cannot be chosen, so it never counts.
  const chosen = CATEGORIES.filter(({ key }) => checked[key] && counts[key] > 0);
  const selected = chosen.reduce((sum, { key }) => sum + counts[key], 0);
  const preview = chosen.flatMap(({ key }) => names[key]);
  return (
    <section className="onboarding-card">
      <Heading eyebrow="Bring your settings" title="Bring your Orca setup">Select the presets to copy into JusPrin.</Heading>
      <SourceCard source={profiles.source} />
      <div className="onboarding-selection">
        <div className="onboarding-selection-summary">
          <span className="onboarding-eyebrow">Presets to import</span>
          <span>{selected} selected</span>
        </div>
        {CATEGORIES.map(({ key, label }) => (
          <label className="onboarding-category" key={key}>
            <span className="onboarding-check">
              <input
                type="checkbox"
                checked={checked[key] && counts[key] > 0}
                disabled={counts[key] === 0}
                onChange={(event) => setChecked({ ...checked, [key]: event.target.checked })}
              />
              <span className="onboarding-check-box" aria-hidden="true"><Glyph name="check" /></span>
              {label}
            </span>
            <span className="onboarding-count">{counts[key]}</span>
          </label>
        ))}
        {preview.length > 0 && (
          <>
            <button
              type="button"
              className="onboarding-disclosure"
              aria-expanded={reviewing}
              onClick={() => setReviewing(!reviewing)}
            >
              <Glyph name={reviewing ? 'chevron-down' : 'chevron-right'} />
              Review selection
            </button>
            {reviewing ? (
              <ul className="onboarding-review">
                {chosen.flatMap(({ key, kind }) => names[key].map((name) => (
                  <li key={`${key}/${name}`}><span>{name}</span><span>{kind}</span></li>
                )))}
              </ul>
            ) : (
              <p className="onboarding-preview">{preview.join(' · ')}</p>
            )}
          </>
        )}
      </div>
      <div className="onboarding-actions">
        <p className="onboarding-copy">Copies settings into JusPrin. Orca stays intact.</p>
        <ActionError error={error} />
        <button
          type="button"
          className="button-primary block"
          disabled={selected === 0}
          onClick={() => onAction('onboarding_use_profiles', { categories: chosen.map(({ key }) => key) })}
        >
          Import selected
        </button>
        <button type="button" className="button-secondary block" onClick={() => onAction('onboarding_choose_profile_bundle')}>
          <Glyph name="upload" />
          Choose preset bundle
        </button>
        <div className="onboarding-nav">
          <LinkButton variant="flush" onClick={() => onAction('onboarding_back')}>Back</LinkButton>
          <LinkButton variant="flush" onClick={() => onAction('onboarding_defer_profiles')}>Skip for now</LinkButton>
        </div>
      </div>
    </section>
  );
}

// The host names a preset by its file inside the Orca user folder,
// "<directory>/<name>.json"; the directory is the preset's kind.
function presetEntry(file: string): { name: string; kind: string } {
  const slash = file.lastIndexOf('/');
  const directory = slash < 0 ? '' : file.slice(0, slash);
  const name = file.slice(slash + 1).replace(/\.json$/i, '');
  return { name, kind: CATEGORIES.find((category) => category.directory === directory)?.kind ?? 'Preset' };
}

function itemCount(count: number): string {
  return count === 1 ? '1 item' : `${count} items`;
}

function ResultList({ title, files, outcome, tone }: { title: string; files: string[]; outcome: string; tone?: 'success' }) {
  return (
    <div className="onboarding-results">
      <div className="onboarding-results-summary">
        <h2>{title}</h2>
        <span>{itemCount(files.length)}</span>
      </div>
      {files.map((file) => {
        const { name, kind } = presetEntry(file);
        return (
          <div className="onboarding-result" key={file}>
            <strong>{name}</strong>
            <span className={tone}>{kind} · {outcome}</span>
          </div>
        );
      })}
    </div>
  );
}

function PartialImport({ onboarding, error, onAction }: Pick<Props, 'onboarding' | 'error' | 'onAction'>) {
  const { profiles } = onboarding;
  const imported = profiles.importedFiles ?? [];
  return (
    <section className="onboarding-card compact">
      <Heading eyebrow="Bring your settings" title="Some settings need attention" />
      <SourceCard source={profiles.source} compact />
      {imported.length > 0 && <ResultList title="Success" files={imported} outcome="copied into JusPrin" tone="success" />}
      <ResultList title="Not imported" files={profiles.failed} outcome="not imported" />
      <p className="onboarding-copy">Orca source data is unchanged. Only the items in Success are available in JusPrin.</p>
      <ActionError error={error} />
      <div className="onboarding-recovery-actions">
        <div className="onboarding-pair">
          <button type="button" className="button-secondary" onClick={() => onAction('onboarding_use_profiles')}>
            Retry failed items
          </button>
          <button type="button" className="button-secondary" onClick={() => onAction('onboarding_choose_profile_bundle')}>
            <Glyph name="upload" />
            Choose preset bundle
          </button>
        </div>
        <button type="button" className="button-primary block" onClick={() => onAction('onboarding_accept_partial_profiles')}>
          Continue with imported settings
        </button>
      </div>
    </section>
  );
}

function NoOrca({ error, onAction }: Pick<Props, 'error' | 'onAction'>) {
  return (
    <section className="onboarding-card">
      <Heading eyebrow="Bring your settings" title="No Orca setup found">
        No supported local Orca configuration was detected. If you don’t have an Orca setup, you can skip this step.
      </Heading>
      <div className="onboarding-source">
        <div className="onboarding-source-name"><Glyph name="monitor" />Import from your local files</div>
        <p className="onboarding-source-path">
          Choose an exported preset bundle, or select your Orca configuration folder manually.
        </p>
      </div>
      <div className="onboarding-actions">
        <p className="onboarding-copy">Continue with the next local setup step. You don’t need imported settings or Agent.</p>
        <ActionError error={error} />
        <button type="button" className="button-primary block" onClick={() => onAction('onboarding_choose_profile_bundle')}>
          Choose preset bundle
        </button>
        <button type="button" className="button-secondary block" onClick={() => onAction('onboarding_choose_profile_folder')}>
          Choose folder manually
        </button>
        <div className="onboarding-nav">
          <LinkButton onClick={() => onAction('onboarding_back')}>Back</LinkButton>
          <LinkButton onClick={() => onAction('onboarding_defer_profiles')}>Skip for now</LinkButton>
        </div>
      </div>
    </section>
  );
}

function ConfirmSetup({ onboarding, error, onAction }: Pick<Props, 'onboarding' | 'error' | 'onAction'>) {
  const { setup, profiles } = onboarding;
  const provenance = profiles.imported ? 'Imported configuration' : 'Saved configuration';
  const fields: [string, string][] = [
    ['Printer', setup.printer],
    ['Nozzle', setup.nozzle],
    ['Plate', setup.plate],
    ['Material', setup.material],
  ];
  return (
    <section className="onboarding-card">
      <Heading eyebrow="Confirm your setup" title="What will you print with?">
        Match these values to the printer and supplies you’ll use.
      </Heading>
      <div className="onboarding-configuration">
        <div className="onboarding-provenance">
          <p className="onboarding-eyebrow">{provenance}</p>
          <p className="onboarding-source-scope">{provenance} · no printer connected</p>
        </div>
        {fields.filter(([, value]) => value).map(([label, value]) => (
          <label className="onboarding-field" key={label}>
            <span>{label}</span>
            <span className="onboarding-select">
              <select aria-label={label} defaultValue={value}><option>{value}</option></select>
              <Glyph name="chevron-down" />
            </span>
          </label>
        ))}
        {setup.process && <p className="onboarding-process">Process preset: {setup.process}</p>}
      </div>
      {setup.plate && (
        <div className="onboarding-notice">
          <strong>Confirm the installed plate</strong>
          <span>The preset uses {setup.plate}. Check the plate on your printer.</span>
        </div>
      )}
      <div className="onboarding-actions">
        <ActionError error={error} />
        <button type="button" className="button-primary block" onClick={() => onAction('onboarding_confirm_setup')}>
          Use this setup
        </button>
        <button type="button" className="button-secondary block" onClick={() => onAction('onboarding_manual_setup')}>
          <Glyph name="printer" />
          Change or find printer
        </button>
        <div className="onboarding-offline">
          <LinkButton variant="flush" onClick={() => onAction('onboarding_offline_example')}>I don’t have a printer here</LinkButton>
          <p className="onboarding-copy">
            Continue with an offline example. You can slice and check it locally; physical Send stays unavailable.
          </p>
        </div>
      </div>
    </section>
  );
}

function ChooseSetup({ error, onAction }: Pick<Props, 'error' | 'onAction'>) {
  return (
    <section className="onboarding-card">
      <Heading eyebrow="Printer setup" title="How would you like to set up your printer?">
        Choose a setup path if you don’t already have a usable configuration. You can continue locally without Agent.
      </Heading>
      <div className="onboarding-choices">
        <div className="onboarding-choice">
          <a
            href={ACCOUNT_STUB}
            className="button-primary block"
            title={ACCOUNT_STUB_NOTE}
            onClick={(event) => {
              event.preventDefault();
              onAction('onboarding_account_stub');
            }}
          >
            Set up with Agent
          </a>
          <p>
            Identify your printer from a photo, model name, or local network. If Agent isn’t configured, connect it and
            return to this task. If it’s temporarily unavailable, retry or choose a local path.
          </p>
        </div>
        <div className="onboarding-choice">
          <button type="button" className="button-secondary block" onClick={() => onAction('onboarding_manual_setup')}>
            Configure manually
          </button>
          <p>Choose the printer model, nozzle, plate, and material yourself.</p>
        </div>
        <div className="onboarding-choice">
          <LinkButton variant="block" onClick={() => onAction('onboarding_offline_example')}>I don’t have a printer here</LinkButton>
          <p>
            Use a labeled offline example. You can prepare and review it locally. Physical Send stays unavailable
            without an eligible printer.
          </p>
        </div>
      </div>
      <ActionError error={error} />
      <div className="onboarding-back">
        <LinkButton variant="block" onClick={() => onAction('onboarding_back')}>Back to Orca import</LinkButton>
        <p>Local setup does not require Agent. Printer configuration and connection are separate.</p>
      </div>
    </section>
  );
}

function ProjectFailed({ reason, onRetry, onChooseAnother, onClose }: {
  reason: string;
  onRetry: () => void;
  onChooseAnother: () => void;
  onClose: () => void;
}) {
  return (
    <section className="onboarding-card onboarding-failure" role="alert">
      <div className="onboarding-heading">
        <div className="onboarding-failure-status">
          <p className="onboarding-eyebrow danger">Project load failed</p>
          <button type="button" className="onboarding-close" aria-label="Close" onClick={onClose}><Glyph name="x" /></button>
        </div>
        <h1>Couldn’t open the file.</h1>
        <p className="onboarding-copy">No existing work was replaced.</p>
      </div>
      <div className="onboarding-reason">
        <strong>Reason</strong>
        <span>{reason}</span>
      </div>
      <div className="onboarding-pair">
        <button type="button" className="button-primary" onClick={onRetry}>Retry</button>
        <button type="button" className="button-secondary" onClick={onChooseAnother}>Choose another</button>
      </div>
    </section>
  );
}

function Project({ error, onAction, onDismissError }: Pick<Props, 'error' | 'onAction' | 'onDismissError'>) {
  // Retry repeats the action that failed, so the candidate is kept.
  const [attempted, setAttempted] = useState<ProjectAction>('onboarding_open_example');
  const attempt = (action: ProjectAction) => {
    setAttempted(action);
    onAction(action);
  };
  if (error) {
    return (
      <ProjectFailed
        reason={error}
        onRetry={() => attempt(attempted)}
        onChooseAnother={() => attempt('import_project')}
        onClose={onDismissError}
      />
    );
  }
  return (
    <section className="onboarding-card">
      <Heading eyebrow="Start a project" title="Start with something you want to print">
        Choose a project or model from your computer, or start with the example.
      </Heading>
      <div className="onboarding-open">
        <button type="button" className="button-primary block" onClick={() => attempt('import_project')}>
          Open a project or model
        </button>
        <p>Opens the file picker · 3MF, STL or OBJ</p>
      </div>
      <div className="onboarding-example">
        <div className="onboarding-example-actions">
          <strong>No file handy?</strong>
          <button type="button" className="button-secondary" onClick={() => attempt('onboarding_open_example')}>
            <Glyph name="box" />
            Try the example
          </button>
        </div>
        <p className="onboarding-copy">Use the labeled example with local tools. No Agent or physical printer is required.</p>
      </div>
      <div className="onboarding-reassurance">
        <p className="onboarding-copy">Your source file stays unchanged.</p>
        <p className="onboarding-copy">Work is saved locally in JusPrin.</p>
      </div>
      <LinkButton variant="flush" onClick={() => onAction('onboarding_back')}>Back</LinkButton>
    </section>
  );
}

export function Onboarding({ onboarding, error, onAction, onDismissError }: Props) {
  let content: React.ReactNode;
  if (onboarding.step === 'welcome') content = <Welcome onAction={onAction} />;
  else if (onboarding.step === 'profiles') {
    if (onboarding.profiles.partialImportPending) content = <PartialImport onboarding={onboarding} error={error} onAction={onAction} />;
    else if (onboarding.profiles.available) content = <OrcaFound onboarding={onboarding} error={error} onAction={onAction} />;
    else content = <NoOrca error={error} onAction={onAction} />;
  } else if (onboarding.step === 'confirm_setup') content = <ConfirmSetup onboarding={onboarding} error={error} onAction={onAction} />;
  else if (onboarding.step === 'setup') content = <ChooseSetup error={error} onAction={onAction} />;
  else content = <Project error={error} onAction={onAction} onDismissError={onDismissError} />;
  return (
    <main className="onboarding">
      <Header onboarding={onboarding} />
      <div className="onboarding-stage">{content}</div>
      <footer className="onboarding-footer">{footerText(onboarding)}</footer>
    </main>
  );
}
