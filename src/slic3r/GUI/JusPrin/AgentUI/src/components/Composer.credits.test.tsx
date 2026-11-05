// The message box with an account: prices in the model menu, greyed pickers once
// the person has sent a message, Send held back when the next message cannot be
// paid, and a picture a Standard chat would not send (boards 2b, 2c, 3a, 3b, 3e, 6a).

import { cleanup, render, screen } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { afterEach, describe, expect, it, vi } from 'vitest';
import { AttachmentInfo, ChatModelChoice, CreditAccount, ModelCatalogPayload } from '../bridge/protocol';
import { lowNotice, messageCostSentence, priceWords, sendBlock } from '../credits';
import { Composer, ComposerCredits } from './Composer';
import { modelMenu } from './ModelPickers';

afterEach(cleanup);

const standard = { id: 'slicer-basic', label: 'Standard', efforts: [], tier: 'basic', credits: 1, images: false };
const expert = { id: 'slicer-1', label: 'Expert', efforts: ['fast', 'thorough'], defaultEffort: 'thorough', tier: 'expert', credits: 5 };
const own = { id: 'gpt-5.4', label: 'gpt-5.4', efforts: [] };
const catalog: ModelCatalogPayload = {
  defaultProvider: 'jusprin',
  providers: [
    { id: 'jusprin', label: 'JusPrin', defaultModel: 'slicer-1', state: 'ready', models: [standard, expert] },
    { id: 'openai', label: 'OpenAI', defaultModel: 'gpt-5.4', state: 'ready', models: [own] },
  ],
};
const onExpert: ChatModelChoice = { provider: 'jusprin', model: 'slicer-1', effort: 'thorough' };
const onStandard: ChatModelChoice = { provider: 'jusprin', model: 'slicer-basic', effort: '' };
const onOwn: ChatModelChoice = { provider: 'openai', model: 'gpt-5.4', effort: '' };

const lot = (kind: string, credits: number) => ({ kind, credits, granted: credits, expires_at: '2026-12-01T12:00:00+00:00' });
const balance = (...lots: ReturnType<typeof lot>[]): CreditAccount => ({
  credits: { total: lots.reduce((sum, one) => sum + one.credits, 0), lots },
  plan: null,
  v1_subscription: null,
});
const workshop = (...lots: ReturnType<typeof lot>[]): CreditAccount => ({
  ...balance(...lots),
  plan: { id: 'jusprin-workshop-monthly', name: 'Workshop', renews_at: '2026-12-01T12:00:00+00:00', unlimited_basic: true },
});

function credits(account: CreditAccount | null, extra: Partial<ComposerCredits> = {}): ComposerCredits {
  return { account, lowNoticeDone: false, onLowNoticeDone: vi.fn(), onGetMore: vi.fn(), onSeeUsage: vi.fn(), ...extra };
}

function renderComposer(props: {
  account?: CreditAccount | null; choice?: ChatModelChoice; started?: boolean; text?: string;
  attachments?: AttachmentInfo[]; composerCredits?: ComposerCredits; onSend?: (text: string) => void;
}) {
  render(<Composer disabled={false} streaming={false} attachments={props.attachments ?? []} initialText={props.text}
    started={props.started} onSend={props.onSend ?? vi.fn()} onStop={vi.fn()} onAttachFiles={vi.fn()} onRemoveAttachment={vi.fn()}
    models={catalog} chatModel={props.choice ?? onExpert} onChooseModel={vi.fn()}
    credits={props.composerCredits ?? credits(props.account ?? balance(lot('signup', 500)))} />);
}

const send = () => screen.getByRole('button', { name: 'Send message' });

describe('prices', () => {
  it('always carry "a message", and only on the account’s models', () => {
    const entries = modelMenu(catalog, onExpert, vi.fn(), balance(lot('signup', 500)));
    expect(entries.flatMap((entry) => ('section' in entry ? [] : [[entry.label, entry.detail]]))).toEqual([
      ['Standard · 1 credit a message', 'setting up an ordinary print'],
      ['Expert · 5 credits a message', 'troubleshooting, tricky models, anything you’d ask a person'],
      ['gpt-5.4', undefined],
    ]);
  });

  it('are one credit a message on Standard with free credits, the ring beside them', () => {
    const account = balance(lot('free', 50));
    expect(priceWords(standard, account)).toBe('1 credit a message');
    expect(priceWords(expert, account)).toBe('5 credits a message');
    expect(messageCostSentence(standard, account)).toBe('Each message you send in this chat costs 1 credit.');
    expect(messageCostSentence(own, account)).toBeUndefined();
    renderComposer({ choice: onStandard, account });
    expect(screen.getByRole('button', { name: 'Credits left' })).toBeTruthy();
    expect(screen.queryByText(/free/)).toBeNull();
  });

  it('are 0 credits a message on Standard on Workshop', () => {
    const account = workshop(lot('subscription', 1000));
    expect(priceWords(standard, account)).toBe('0 credits a message');
    expect(priceWords(expert, account)).toBe('5 credits a message');
    expect(messageCostSentence(standard, account)).toBe('Each message you send in this chat costs 0 credits.');
  });
});

describe('the pickers', () => {
  it('work until the person sends a message', () => {
    renderComposer({});
    expect(screen.getByRole('button', { name: /^Model/ })).toBeEnabled();
    expect(screen.queryByRole('tooltip', { hidden: true })).toBeNull();
  });

  it('are greyed afterwards and say why and what a message costs', () => {
    renderComposer({ started: true });
    expect(screen.getByRole('button', { name: /^Model/ })).toBeDisabled();
    expect(screen.getByRole('button', { name: /^Thinking/ })).toBeDisabled();
    const tooltip = screen.getByRole('tooltip', { hidden: true });
    expect(tooltip).toHaveTextContent('A chat keeps the model and thinking level it started with. Start a new chat to change them.');
    expect(tooltip).toHaveTextContent('Each message you send in this chat costs 5 credits.');
  });

  it('offer no thinking level on Standard', () => {
    renderComposer({ choice: onStandard });
    expect(screen.getByRole('button', { name: /^Model/ })).toBeEnabled();
    expect(screen.queryByRole('button', { name: /^Thinking/ })).toBeNull();
  });

  it('name no price on the person’s own key, and draw no ring', () => {
    renderComposer({ started: true, choice: onOwn });
    const tooltip = screen.getByRole('tooltip', { hidden: true });
    expect(tooltip).toHaveTextContent('A chat keeps the model');
    expect(tooltip).not.toHaveTextContent('credit');
    expect(screen.queryByRole('button', { name: 'Credits left' })).toBeNull();
  });
});

describe('the check before sending', () => {
  it('leaves Send alone when the message can be paid', () => {
    renderComposer({ text: 'Hello', account: balance(lot('signup', 100)) });
    expect(send()).toBeEnabled();
    expect(screen.queryByRole('button', { name: 'Get more credits' })).toBeNull();
  });

  it('disables Send, says why, offers one action and keeps the words', () => {
    const composerCredits = credits(balance(lot('pack', 3)));
    renderComposer({ text: 'The tip curled up', composerCredits });
    expect(send()).toBeDisabled();
    expect(screen.getByText('You have 3 credits; this Expert chat needs 5 a message.')).toBeTruthy();
    expect(screen.getByRole('button', { name: 'Get more credits' })).toBeTruthy();
    expect(screen.getAllByRole('button').filter((button) => /chat/i.test(button.textContent ?? ''))).toEqual([]);
    expect(screen.getByRole('textbox')).toHaveValue('The tip curled up');
  });

  it('lets free credits pay for Expert like any other credit', () => {
    expect(sendBlock(balance(lot('free', 50)), expert)).toBeUndefined();
    expect(sendBlock(balance(lot('free', 4)), expert)).toBe('You have 4 credits; this Expert chat needs 5 a message.');
    expect(sendBlock(balance(lot('free', 4)), standard)).toBeUndefined();
  });

  it('never stops Standard on Workshop, even with no credits', () => {
    expect(sendBlock(workshop(), standard)).toBeUndefined();
    expect(sendBlock(workshop(), expert)).toBe('You have 0 credits; this Expert chat needs 5 a message.');
  });

  it('never blocks a model on the person’s own key', () => {
    expect(sendBlock(balance(), own)).toBeUndefined();
  });

  it('says once that few credits are left, in credits, with Get more credits', async () => {
    let done = false;
    const composerCredits = credits(balance(lot('pack', 25)), { onLowNoticeDone: () => { done = true; } });
    const onSend = vi.fn();
    renderComposer({ text: 'Hello', composerCredits, onSend });
    expect(screen.getByText('About 25 credits left')).toBeTruthy();
    expect(document.querySelector('.credit-ring--low')).toBeTruthy();
    await userEvent.click(screen.getByRole('button', { name: 'Get more credits' }));
    expect(composerCredits.onGetMore).toHaveBeenCalledOnce();
    await userEvent.click(send());
    expect(onSend).toHaveBeenCalledWith('Hello');
    expect(done).toBe(true);
  });

  it('lets the low notice be closed, and keeps the warning colour afterwards', async () => {
    const onLowNoticeDone = vi.fn();
    renderComposer({ composerCredits: credits(balance(lot('pack', 25)), { onLowNoticeDone }) });
    await userEvent.click(screen.getByRole('button', { name: 'Close' }));
    expect(onLowNoticeDone).toHaveBeenCalledOnce();
    cleanup();
    renderComposer({ composerCredits: credits(balance(lot('pack', 25)), { lowNoticeDone: true }) });
    expect(screen.queryByText('About 25 credits left')).toBeNull();
    expect(document.querySelector('.credit-ring--low')).toBeTruthy();
  });

  it('shows the low notice at 25 credits on any model that charges, rounded to the nearest 5', () => {
    expect(lowNotice(balance(lot('pack', 23)), expert)).toBe('About 25 credits left');
    expect(lowNotice(balance(lot('pack', 26)), expert)).toBeUndefined();
    expect(lowNotice(balance(lot('pack', 4)), expert)).toBeUndefined();
    expect(lowNotice(balance(lot('pack', 25)), standard)).toBe('About 25 credits left');
    expect(lowNotice(balance(lot('pack', 26)), standard)).toBeUndefined();
    expect(lowNotice(balance(lot('pack', 5)), standard)).toBe('About 5 credits left');
    expect(lowNotice(balance(lot('pack', 1)), standard)).toBe('About 5 credits left');
    expect(lowNotice(workshop(lot('subscription', 1)), standard)).toBeUndefined();
  });
});

describe('a picture in a Standard chat', () => {
  const picture: AttachmentInfo = { id: 'a1', name: 'curl.jpg', kind: 'image', mime: 'image/jpeg', sizeBytes: 2048, state: 'staged', summary: '' } as AttachmentInfo;

  it('says it will not be sent, as a warning, with Remove the only action, and Send stays enabled', () => {
    renderComposer({ text: 'Why?', choice: onStandard, attachments: [picture], composerCredits: credits(balance(lot('signup', 500))) });
    expect(screen.getByText('Standard can’t see pictures, so this one will not be sent.')).toBeTruthy();
    expect(document.querySelector('.attachment-not-sent .jp-icon-triangle-alert')).toBeTruthy();
    expect(screen.getByRole('button', { name: 'Remove curl.jpg' })).toBeTruthy();
    expect(screen.queryByRole('button', { name: /Expert chat/ })).toBeNull();
    expect(send()).toBeEnabled();
  });

  it('says nothing in an Expert chat', () => {
    renderComposer({ text: 'Why?', attachments: [picture] });
    expect(screen.queryByText(/can’t see pictures/)).toBeNull();
  });
});
