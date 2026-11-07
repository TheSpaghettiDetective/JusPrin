// What the account's credits mean for the chat in front of the person: whether
// the next message can be paid, and the words for a price. The gateway keeps the
// ledger and still decides; this only lets the page say so before sending.

import { CreditAccount, ModelOptionInfo } from './bridge/protocol';

// 1,000 with its comma, whatever language the machine is set to.
export const creditNumber = (count: number) => count.toLocaleString('en-US');

export const creditCount = (count: number) => `${creditNumber(count)} ${count === 1 ? 'credit' : 'credits'}`;

// What a message on this model costs: its price, but nothing for a Standard
// message on a plan that includes them.
function messageCost(model: ModelOptionInfo, account: CreditAccount | null | undefined): number | undefined {
  if (model.credits === undefined) return undefined;
  return model.tier === 'basic' && account?.plan?.unlimited_basic ? 0 : model.credits;
}

export function priceWords(model: ModelOptionInfo, account: CreditAccount | null | undefined): string | undefined {
  const cost = messageCost(model, account);
  return cost === undefined ? undefined : `${creditCount(cost)} a message`;
}

// The second sentence on the greyed pickers' tooltip.
export function messageCostSentence(model: ModelOptionInfo | undefined, account: CreditAccount | null | undefined): string | undefined {
  const cost = model && messageCost(model, account);
  return cost === undefined ? undefined : `Each message you send in this chat costs ${creditCount(cost)}.`;
}

// The sign-up offer, wherever someone can make an account. The gateway grants
// a new account 500 credits (JUSPRIN_V2_SIGNUP_CREDITS) for a month from the
// day it is granted, and an Expert message costs 5 (its model aliases,
// checked on staging 2026-11-05). Nothing asks the gateway before sign-in.
// No plan is named: someone who has not signed up has none to choose.
export const SIGNUP_CREDITS = 500;
const EXPERT_MESSAGE_CREDITS = 5;
export const SIGNUP_OFFER = `Sign up and get ${creditNumber(SIGNUP_CREDITS)} free credits`;
export const SIGNUP_SCALE =
  `Good for your first month. That’s about ${SIGNUP_CREDITS / EXPERT_MESSAGE_CREDITS} messages to AI.`;
// What a free account gets once the first month is over, under the offer
// where it is drawn at rest. 50 a month is the free plan's monthly grant,
// confirmed for the welcome on 2026-11-06; nothing here reads it.
export const SIGNUP_AFTER = 'After that, 50 free credits a month. No card needed.';

// "4 December".
const longDate = (iso: string) => new Date(iso).toLocaleDateString('en-GB', { day: 'numeric', month: 'long' });

// What signing in got the person, the line above who signed in: the free
// credits a new account was just given, or what an account they used before
// has left.
export function signedInCredits(account: CreditAccount, now: Date = new Date()): string {
  const signup = account.credits.lots.find((lot) => lot.kind === 'signup');
  if (signup?.expires_at && signup.credits === signup.granted && new Date(signup.expires_at) > now)
    return `You got ${creditNumber(signup.granted)} free credits for signing up, good until ${longDate(signup.expires_at)}.`;
  return `You have ${creditCount(account.credits.total)}.`;
}

export const TIER_NAMES: Record<string, string> = { basic: 'Standard', expert: 'Expert' };

// Why the next message of this chat cannot be paid, or nothing when it can.
export function sendBlock(account: CreditAccount | null | undefined, model: ModelOptionInfo | undefined): string | undefined {
  if (!account || !model?.tier) return undefined;
  const cost = messageCost(model, account);
  if (cost === undefined || account.credits.total >= cost) return undefined;
  return `You have ${creditCount(account.credits.total)}; this ${TIER_NAMES[model.tier] ?? model.label} chat needs ${creditNumber(cost)} a message.`;
}

// "About 25 credits left", once the balance is 25 credits or less on any model
// that charges. The number is rounded to the nearest 5, because it says "about".
const LOW_CREDITS = 25;

export function lowNotice(account: CreditAccount | null | undefined, model: ModelOptionInfo | undefined): string | undefined {
  if (!account || !model?.tier) return undefined;
  const cost = messageCost(model, account);
  const total = account.credits.total;
  if (!cost || total < cost || total > LOW_CREDITS) return undefined;
  return `About ${Math.max(Math.round(total / 5) * 5, 5)} credits left`;
}
