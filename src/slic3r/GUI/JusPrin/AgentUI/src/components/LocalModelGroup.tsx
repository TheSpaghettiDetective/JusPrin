// Preferences › BYOAI's group for a model the person runs on this computer
// (canvas "JusPrin v2": Agent/A Model on This Computer, approved 2026-11-24).
// The host finds the models at an address and checks the one picked; the
// page only mirrors where that has got to (local_status) and what is stored
// (setup_keys' local).

import { KeyboardEvent, useEffect, useRef, useState } from 'react';
import { plural, t } from '@lingui/core/macro';
import { Trans } from '@lingui/react/macro';
import { LocalModelChoice, LocalModelInfo, LocalStatusPayload } from '../bridge/protocol';

export const OLLAMA_ADDRESS = 'http://127.0.0.1:11434';

// The address as the sentences show it: without http:// and with no path.
export function shownAddress(address: string): string {
  return address.replace(/^[a-z]+:\/\//i, '').replace(/\/.*$/, '');
}

export interface LocalModelGroupProps {
  saved: LocalModelInfo | null;
  status: LocalStatusPayload;
  onFind: (address: string) => void;
  onCheck: (model: string) => void;
  onCancel: () => void;
  onRemove: () => void;
  // Opens the guide's section on a model's tools and context.
  onHelp: () => void;
}

interface FoundList {
  address: string;
  runtime: string;
  models: LocalModelChoice[];
}

function connectedLine(runtime: string, address: string): string {
  if (runtime === 'Ollama') return t`Ollama at ${address}`;
  if (runtime === 'LM Studio') return t`LM Studio at ${address}`;
  return t`The app at ${address}`;
}

function listLine(list: FoundList): string {
  const address = shownAddress(list.address);
  const count = list.models.length;
  if (list.runtime === 'Ollama') {
    if (count === 0) return t`Ollama at ${address} has no models. Download one in Ollama, then find models again.`;
    return plural(count, {
      one: `Ollama at ${address} has # model. Pick the one your chats should use.`,
      other: `Ollama at ${address} has # models. Pick the one your chats should use.`,
    });
  }
  if (list.runtime === 'LM Studio') {
    if (count === 0) return t`LM Studio at ${address} has no models. Download one in LM Studio, then find models again.`;
    return plural(count, {
      one: `LM Studio at ${address} has # model. Pick the one your chats should use.`,
      other: `LM Studio at ${address} has # models. Pick the one your chats should use.`,
    });
  }
  if (count === 0) return t`The app at ${address} has no models. Download one in that app, then find models again.`;
  return plural(count, {
    one: `The app at ${address} has # model. Pick the one your chats should use.`,
    other: `The app at ${address} has # models. Pick the one your chats should use.`,
  });
}

function requirementLine(runtime: string): string {
  if (runtime === 'Ollama') return t`The model must support tools, and its context length in Ollama must be at least 32k.`;
  if (runtime === 'LM Studio') return t`The model must support tools, and its context length in LM Studio must be at least 32k.`;
  return t`The model must support tools, and its context length in the app that runs it must be at least 32k.`;
}

export function LocalModelGroup({ saved, status, onFind, onCheck, onCancel, onRemove, onHelp }: LocalModelGroupProps) {
  const [form, setForm] = useState<'closed' | 'address' | 'models'>('closed');
  const [address, setAddress] = useState(OLLAMA_ADDRESS);
  const [list, setList] = useState<FoundList | null>(null);
  const [picked, setPicked] = useState('');
  // Which request the host's status speaks for: the page's last one, until
  // the person changes what it was about.
  const [sent, setSent] = useState<'find' | 'check' | null>(null);
  const input = useRef<HTMLInputElement>(null);

  const close = () => {
    setForm('closed');
    setList(null);
    setSent(null);
  };

  useEffect(() => {
    if (sent === 'find' && status.phase === 'models') {
      const found = { address: status.address ?? address, runtime: status.runtime ?? '', models: status.models ?? [] };
      setList(found);
      setForm('models');
      const keep = found.models.find((model) => model.id === saved?.model);
      setPicked(keep ? keep.id : found.models[0]?.id ?? '');
      setSent(null);
    }
    if (sent === 'check' && status.phase === 'verified') close();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [status]);

  useEffect(() => {
    if (form === 'address') input.current?.focus();
  }, [form]);

  const finding = sent === 'find' && status.phase === 'finding';
  const checking = sent === 'check' && status.phase === 'checking';
  const failed = status.phase === 'error' ? status.error?.message : undefined;
  const addressError = form === 'address' && sent === 'find' ? failed : undefined;
  const checkError = form === 'models' && sent === 'check' ? failed : undefined;

  const find = (from: string) => {
    if (!from.trim() || finding) return;
    setSent('find');
    onFind(from.trim());
  };
  const cancel = () => {
    if (finding || checking) onCancel();
    close();
  };
  const keyDown = (event: KeyboardEvent<HTMLInputElement>) => {
    if (event.key === 'Enter') find(address);
    if (event.key === 'Escape') cancel();
  };

  // The saved model stays what chats use while another is being found and
  // checked, so the row keeps saying so.
  const connected = saved !== null;
  const model = saved?.model ?? '';
  const pickedModel = picked;
  const typedAddress = shownAddress(address);

  return (
    <section className="agent-group" aria-label={t`A model on this computer`}>
      <div className="agent-group-head">
        <h2 className="agent-group-title">{t`A model on this computer`}</h2>
        <p className="agent-group-intro">
          {t`Use a model you run yourself with Ollama, LM Studio or a similar app. Your messages go to that app, not to an online AI service, and there is nothing to pay. Local models are slower and make more mistakes than the cloud models above.`}
        </p>
      </div>
      <div className="agent-row" data-testid="agent-local">
        <div className="agent-row-line">
          <div className="agent-row-copy">
            <span className="agent-row-name">{t`Local model`}</span>
            {connected ? (
              <>
                <span className="agent-row-status agent-row-status--saved" data-testid="agent-local-standing">
                  <span className="agent-row-status-label">
                    <span className="jp-icon jp-icon-check" aria-hidden="true" />
                    <span>{t`Chats can use`}</span>
                  </span>
                  <code className="agent-key-hint">{model}</code>
                </span>
                <span className="agent-row-status agent-local-where">{connectedLine(saved.runtime, saved.address)}</span>
              </>
            ) : (
              <span className="agent-row-status">{t`Not set up`}</span>
            )}
          </div>
          {form === 'closed' && (saved === null ? (
            <button type="button" className="agent-row-action" onClick={() => setForm('address')}>
              <span className="jp-icon jp-icon-plus" aria-hidden="true" />
              {t`Set up`}
            </button>
          ) : (
            <div className="agent-row-actions">
              <button type="button" className="agent-row-action" onClick={() => {
                const from = `http://${saved.address}`;
                setAddress(from);
                setForm('address');
                find(from);
              }}>{t`Change`}</button>
              <button type="button" className="agent-row-action agent-row-action--danger" onClick={onRemove}>{t`Remove`}</button>
            </div>
          ))}
        </div>

        {connected && form === 'closed' && !saved.images && (
          <p className="agent-row-note">{t`This model can’t see pictures, so the AI works without seeing your model.`}</p>
        )}

        {form === 'address' && (
          <div className="agent-key-form">
            <label className="agent-key-label" htmlFor="agent-local-address">{t`Address of the app that runs the model`}</label>
            <div className={`agent-key-field${finding ? ' agent-key-field--locked' : addressError ? ' agent-key-field--error' : ''}`}>
              <input ref={input} id="agent-local-address" className="agent-key-input" value={address} disabled={finding}
                readOnly={finding} aria-invalid={addressError ? true : undefined} spellCheck={false}
                onChange={(event) => {
                  setAddress(event.target.value);
                  setSent(null);
                }}
                onKeyDown={keyDown} />
            </div>
            {addressError ? (
              <p className="agent-key-error" role="alert" data-testid="local-error">{addressError}</p>
            ) : (
              <p className="agent-row-note">
                <Trans>This is Ollama’s address. For LM Studio, use <code className="agent-key-hint">http://127.0.0.1:1234</code>.</Trans>
              </p>
            )}
            <div className="agent-key-actions">
              {finding ? (
                <span className="agent-key-checking">{t`Looking for models at ${typedAddress}…`}</span>
              ) : (
                <button type="button" className="primary" onClick={() => find(address)} disabled={!address.trim()}>
                  {t`Find models`}
                </button>
              )}
              <button type="button" className="link" onClick={cancel}>{t`Cancel`}</button>
            </div>
          </div>
        )}

        {form === 'models' && list && (
          <div className="agent-key-form">
            <p className="agent-row-note">{listLine(list)}</p>
            {list.models.length > 0 && (
              <fieldset className="agent-local-list" disabled={checking} aria-label={t`Model`}>
                {list.models.map((choice) => (
                  <label key={choice.id}
                    className={`agent-local-option${choice.id === picked ? ' agent-local-option--picked' : ''}`}>
                    <input type="radio" name="agent-local-model" checked={choice.id === picked}
                      onChange={() => {
                        setPicked(choice.id);
                        setSent(null);
                      }} />
                    <span className="agent-local-option-copy">
                      <code className="agent-local-option-name">{choice.id}</code>
                      <span className="agent-local-option-detail">
                        {choice.images ? t`Can see pictures` : t`Can’t see pictures. The AI works without seeing your model.`}
                      </span>
                    </span>
                  </label>
                ))}
              </fieldset>
            )}
            <p className="agent-row-note">
              {requirementLine(list.runtime)}{' '}
              <button type="button" className="link prefs-inline-link" onClick={onHelp}>{t`How to check and change this`}</button>
            </p>
            {checkError && <p className="agent-key-error" role="alert" data-testid="local-error">{checkError}</p>}
            <div className="agent-key-actions">
              {checking ? (
                <span className="agent-key-checking">
                  {t`Checking ${pickedModel}. A local model can take a minute to answer the first time.`}
                </span>
              ) : (
                list.models.length > 0 && (
                  <button type="button" className="primary" disabled={!picked} onClick={() => {
                    setSent('check');
                    onCheck(picked);
                  }}>{t`Use this model`}</button>
                )
              )}
              <button type="button" className="link" onClick={cancel}>{t`Cancel`}</button>
            </div>
          </div>
        )}
      </div>
    </section>
  );
}
