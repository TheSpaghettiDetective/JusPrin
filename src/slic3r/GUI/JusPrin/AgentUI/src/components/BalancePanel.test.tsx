// The credits ring and the panel it opens: the total, then a bar per lot, soonest
// to expire first (board 2a).

import { cleanup, render, screen } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { afterEach, describe, expect, it, vi } from 'vitest';
import { CreditAccount } from '../bridge/protocol';
import { BalancePanel, ringShare } from './BalancePanel';

afterEach(cleanup);

const board2a: CreditAccount = {
  credits: {
    total: 562,
    lots: [
      { kind: 'pack', credits: 500, granted: 500, expires_at: null },
      { kind: 'signup', credits: 62, granted: 500, expires_at: '2026-12-01T12:00:00+00:00' },
    ],
  },
  plan: null,
  v1_subscription: null,
  usage_30d: { credits: 0, days: [], activity: [] },
};

function open(account: CreditAccount, onGetMore = vi.fn(), onSeeUsage = vi.fn()) {
  render(<BalancePanel account={account} low={false} onGetMore={onGetMore} onSeeUsage={onSeeUsage} />);
  return userEvent.click(screen.getByRole('button', { name: 'Credits left' }));
}

const lotRows = () => [...document.querySelectorAll('.credit-lot')].map((row) => row.textContent);

describe('the balance panel', () => {
  it('shows no number until it is opened', () => {
    render(<BalancePanel account={board2a} low={false} onGetMore={vi.fn()} onSeeUsage={vi.fn()} />);
    expect(screen.getByRole('button', { name: 'Credits left' })).toHaveTextContent('');
    expect(screen.queryByText(/credits left/)).toBeNull();
  });

  it('says the balance as one number of credits, with no count of messages', async () => {
    await open(board2a);
    expect(screen.getByText('562 credits left')).toBeTruthy();
    expect(document.querySelector('.credit-panel')?.textContent).not.toMatch(/message/i);
  });

  it('gives every lot a bar, soonest to expire first and bought credits last', async () => {
    await open({
      ...board2a,
      credits: {
        total: 612,
        lots: [...board2a.credits.lots, { kind: 'free', credits: 50, granted: 50, expires_at: '2026-12-02T12:00:00+00:00' }],
      },
    });
    expect(lotRows()).toEqual([
      'First-month credits62 of 500 leftExpires 1 Dec',
      'Free credits50 of 50 leftExpires 2 Dec',
      'Bought credits500 of 500 leftNever expire',
    ]);
    expect(document.querySelectorAll('.credit-lot-bar')).toHaveLength(3);
    expect(screen.getByText('Expiring credits are used first.')).toBeTruthy();
  });

  it('puts two packs on one bar, because they share an expiry', async () => {
    await open({
      ...board2a,
      credits: {
        total: 800,
        lots: [{ kind: 'pack', credits: 300, granted: 500, expires_at: null }, { kind: 'pack', credits: 500, granted: 500, expires_at: null }],
      },
    });
    expect(lotRows()).toEqual(['Bought credits800 of 1,000 leftNever expire']);
  });

  it('says plan credits do not carry over', async () => {
    await open({
      credits: { total: 640, lots: [{ kind: 'subscription', credits: 640, granted: 1000, expires_at: '2026-12-01T12:00:00+00:00' }] },
      plan: { id: 'jusprin-workshop-monthly', name: 'Workshop', renews_at: '2026-12-01T12:00:00+00:00', unlimited_basic: true },
      v1_subscription: null,
      usage_30d: { credits: 0, days: [], activity: [] },
    });
    expect(lotRows()).toEqual(['Workshop credits640 of 1,000 leftExpires 1 Dec. Unused credits don’t carry over.']);
  });

  it('offers Get more credits and See usage', async () => {
    const onGetMore = vi.fn();
    const onSeeUsage = vi.fn();
    await open(board2a, onGetMore, onSeeUsage);
    await userEvent.click(screen.getByRole('button', { name: 'Get more credits' }));
    await userEvent.click(screen.getByRole('button', { name: 'See usage' }));
    expect(onGetMore).toHaveBeenCalledOnce();
    expect(onSeeUsage).toHaveBeenCalledOnce();
  });

  it('measures the ring against what the lots started with', () => {
    expect(ringShare(board2a)).toBeCloseTo(562 / 1000);
  });
});
