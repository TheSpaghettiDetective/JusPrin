// The composer's credits ring and the panel it opens (board 2a on the "JusPrin
// v2" canvas): a small ring of what is left, no number and no animation;
// hovering or tapping opens the total, then a bar per lot, soonest to expire
// first. Lots that never expire share an expiry, so those of one kind share a bar.
//
// What the ring measures against is open (plan decision 13): the sum of what
// each lot started with, the board's placeholder.

import { useEffect, useRef, useState } from 'react';
import { CreditAccount, CreditLot } from '../bridge/protocol';
import { creditCount, creditNumber } from '../credits';

const RING = 16;
const STROKE = 3;

function shortDate(iso: string): string {
  return new Date(iso).toLocaleDateString('en-GB', { day: 'numeric', month: 'short' });
}

function lotName(lot: CreditLot, account: CreditAccount): string {
  switch (lot.kind) {
    case 'signup': return 'Sign-up credits';
    case 'free': return 'Free credits';
    case 'subscription': return `${account.plan?.name ?? 'Plan'} credits`;
    case 'pack': return 'Bought credits';
    // What to call a reward is open (plan decision 13).
    case 'reward': return 'Report credits';
    default: return 'Credits';
  }
}

export interface Row { key: string; name: string; left: number; of: number; date: string }

// Also Home's account card, which draws the same bars.
export function balanceRows(account: CreditAccount): Row[] {
  const expiring = account.credits.lots.filter((lot) => lot.expires_at).map((lot) => ({
    key: `${lot.kind}-${lot.expires_at}`, name: lotName(lot, account), left: lot.credits, of: lot.granted, at: lot.expires_at!,
    date: `Expires ${shortDate(lot.expires_at!)}${lot.kind === 'subscription' ? '. Unused credits don’t carry over.' : ''}`,
  }));
  expiring.sort((a, b) => a.at.localeCompare(b.at));
  const never = new Map<string, Row>();
  for (const lot of account.credits.lots.filter((one) => !one.expires_at)) {
    const name = lotName(lot, account);
    const row = never.get(name) ?? { key: name, name, left: 0, of: 0, date: 'Never expire' };
    never.set(name, { ...row, left: row.left + lot.credits, of: row.of + lot.granted });
  }
  return [...expiring, ...never.values()];
}

// "Expiring credits are used first." says which bar goes down first, so it is
// shown only with two or more bars.
export const EXPIRING_FIRST = 'Expiring credits are used first.';
export const showsExpiringFirst = (rows: Row[]) => rows.length >= 2;

// The ring's share: what is left of what the lots started with.
export function ringShare(account: CreditAccount): number {
  const started = account.credits.lots.reduce((sum, lot) => sum + lot.granted, 0);
  return started > 0 ? Math.min(account.credits.total / started, 1) : 0;
}

function Ring({ share, low }: { share: number; low: boolean }) {
  const radius = (RING - STROKE) / 2;
  const circumference = 2 * Math.PI * radius;
  return <svg className={low ? 'credit-ring credit-ring--low' : 'credit-ring'} viewBox={`0 0 ${RING} ${RING}`} aria-hidden="true">
    <circle className="credit-ring-track" cx={RING / 2} cy={RING / 2} r={radius} fill="none" strokeWidth={STROKE} />
    <circle className="credit-ring-fill" cx={RING / 2} cy={RING / 2} r={radius} fill="none" strokeWidth={STROKE}
      strokeDasharray={`${share * circumference} ${circumference}`} transform={`rotate(-90 ${RING / 2} ${RING / 2})`} />
  </svg>;
}

export function BalancePanel({ account, low, onGetMore, onSeeUsage }: {
  account: CreditAccount;
  low: boolean;
  onGetMore: () => void;
  onSeeUsage: () => void;
}) {
  const [open, setOpen] = useState(false);
  const closing = useRef<ReturnType<typeof setTimeout> | null>(null);
  useEffect(() => () => { if (closing.current !== null) clearTimeout(closing.current); }, []);
  const keepOpen = () => {
    if (closing.current !== null) clearTimeout(closing.current);
    closing.current = null;
    setOpen(true);
  };
  // The pointer crosses a gap on its way from the ring to the panel's actions.
  const closeSoon = () => { closing.current = setTimeout(() => setOpen(false), 200); };
  const rows = balanceRows(account);

  return <div className="credit-balance" onMouseEnter={keepOpen} onMouseLeave={closeSoon}
    onKeyDown={(event) => { if (event.key === 'Escape') setOpen(false); }}>
    <button type="button" className="credit-ring-button" aria-label="Credits left" aria-expanded={open}
      onClick={keepOpen}>
      <Ring share={ringShare(account)} low={low} />
    </button>
    {open && <div className="credit-panel" role="dialog" aria-label="Credits">
      <div className="credit-panel-total">
        <strong>{creditCount(account.credits.total)} left</strong>
      </div>
      <div className="credit-panel-lots">
        {rows.map((row) => <div key={row.key} className="credit-lot">
          <div className="credit-lot-line">
            <strong>{row.name}</strong>
            <span>{creditNumber(row.left)} of {creditNumber(row.of)} left</span>
          </div>
          <div className="credit-lot-bar">
            <div className="credit-lot-fill" style={{ width: `${row.of > 0 ? Math.min(row.left / row.of, 1) * 100 : 0}%` }} />
          </div>
          <span className="credit-lot-date">{row.date}</span>
        </div>)}
        {showsExpiringFirst(rows) && <p className="credit-panel-rule">{EXPIRING_FIRST}</p>}
      </div>
      <div className="credit-panel-actions">
        <button type="button" className="link" onClick={onGetMore}>Get more credits</button>
        <button type="button" className="link" onClick={onSeeUsage}>See usage</button>
      </div>
    </div>}
  </div>;
}
