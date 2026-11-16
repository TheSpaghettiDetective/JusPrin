// What the account's credits mean for the chat in front of the person: whether
// the next message can be paid, and the words for a price. The gateway keeps the
// ledger and still decides; this only lets the page say so before sending.

import { plural, t } from '@lingui/core/macro';
import { CreditAccount, ModelOptionInfo } from './bridge/protocol';
import { dateLocale } from './i18n';

// 1,000 with its comma, whatever language the machine is set to.
export const creditNumber = (count: number) => count.toLocaleString(dateLocale('en-US'));

export function creditCount(count: number): string {
  const n = creditNumber(count);
  return plural(count, { one: `${n} credit`, other: `${n} credits` });
}

// "1,000 credits left", the balance where it stands on its own.
export function creditsLeft(count: number): string {
  const n = creditNumber(count);
  return plural(count, { one: `${n} credit left`, other: `${n} credits left` });
}

// What a message on this model costs: its price, but nothing for a Standard
// message on a plan that includes them.
function messageCost(model: ModelOptionInfo, account: CreditAccount | null | undefined): number | undefined {
  if (model.credits === undefined) return undefined;
  return model.tier === 'basic' && account?.plan?.unlimited_basic ? 0 : model.credits;
}

export function priceWords(model: ModelOptionInfo, account: CreditAccount | null | undefined): string | undefined {
  const cost = messageCost(model, account);
  if (cost === undefined) return undefined;
  const n = creditNumber(cost);
  return plural(cost, { one: `${n} credit a message`, other: `${n} credits a message` });
}

// The second sentence on the greyed pickers' tooltip.
export function messageCostSentence(model: ModelOptionInfo | undefined, account: CreditAccount | null | undefined): string | undefined {
  const cost = model && messageCost(model, account);
  if (cost === undefined) return undefined;
  const n = creditNumber(cost);
  return plural(cost, {
    one: `Each message you send in this chat costs ${n} credit.`,
    other: `Each message you send in this chat costs ${n} credits.`,
  });
}

// TRN: the names of the two kinds of AI model a chat can run on.
export const TIER_NAMES: Record<string, string> = {
  basic: t({ message: 'Standard', comment: 'The cheaper kind of AI model' }),
  expert: t({ message: 'Expert', comment: 'The more capable kind of AI model' }),
};

// The sign-up offer, wherever someone can make an account. The gateway grants
// a new account 500 credits (JUSPRIN_V2_SIGNUP_CREDITS) for a month from the
// day it is granted. A Standard message costs 1 and an Expert message 5 (the
// gateway's model aliases, checked on staging 2026-11-07). Nothing asks the
// gateway before sign-in.
// No plan is named: someone who has not signed up has none to choose.
export const SIGNUP_CREDITS = 500;
const STANDARD_MESSAGE_CREDITS = 1;
const EXPERT_MESSAGE_CREDITS = 5;
const signupCredits = creditNumber(SIGNUP_CREDITS);
export const SIGNUP_OFFER = t`Sign up and get ${signupCredits} free credits`;
const standardMessages = SIGNUP_CREDITS / STANDARD_MESSAGE_CREDITS;
const expertMessages = SIGNUP_CREDITS / EXPERT_MESSAGE_CREDITS;
// TRN: Standard and Expert are the two kinds of AI model, named as elsewhere.
export const SIGNUP_SCALE = t`Good for your first month: about ${standardMessages} Standard or ${expertMessages} Expert messages.`;
// The price of a message on Account. GET /account carries no prices, so these
// are the same constants as the offer's.
// TRN: Standard and Expert are the two kinds of AI model, named as elsewhere.
export const MESSAGE_PRICES = t`Each message you send costs credits: Standard ${STANDARD_MESSAGE_CREDITS}, Expert ${EXPERT_MESSAGE_CREDITS}.`;
// What a free account gets once the first month is over, under the offer
// where it is drawn at rest. 50 a month is the free plan's monthly grant,
// confirmed for the welcome on 2026-11-06; nothing here reads it.
export const SIGNUP_AFTER = t`After that, 50 free credits a month. No card needed.`;

// "4 December".
const longDate = (iso: string) => new Date(iso).toLocaleDateString(dateLocale('en-GB'), { day: 'numeric', month: 'long' });

// What signing in got the person, the line above who signed in: the free
// credits a new account was just given, or what an account they used before
// has left.
export function signedInCredits(account: CreditAccount, now: Date = new Date()): string {
  const signup = account.credits.lots.find((lot) => lot.kind === 'signup');
  if (signup?.expires_at && signup.credits === signup.granted && new Date(signup.expires_at) > now) {
    const granted = creditNumber(signup.granted);
    const until = longDate(signup.expires_at);
    return plural(signup.granted, {
      one: `You got ${granted} free credit for signing up, good until ${until}.`,
      other: `You got ${granted} free credits for signing up, good until ${until}.`,
    });
  }
  const total = account.credits.total;
  const n = creditNumber(total);
  return plural(total, { one: `You have ${n} credit.`, other: `You have ${n} credits.` });
}

// Why the next message of this chat cannot be paid, or nothing when it can.
export function sendBlock(account: CreditAccount | null | undefined, model: ModelOptionInfo | undefined): string | undefined {
  if (!account || !model?.tier) return undefined;
  const cost = messageCost(model, account);
  if (cost === undefined || account.credits.total >= cost) return undefined;
  const total = account.credits.total;
  const n = creditNumber(total);
  const tier = TIER_NAMES[model.tier] ?? model.label;
  const needed = creditNumber(cost);
  // TRN: {tier} is Standard, Expert or a model's name; {needed} counts credits.
  return plural(total, {
    one: `You have ${n} credit; this ${tier} chat needs ${needed} a message.`,
    other: `You have ${n} credits; this ${tier} chat needs ${needed} a message.`,
  });
}

// "About 25 credits left", once the balance is 25 credits or less on any model
// that charges. The number is rounded to the nearest 5, because it says "about".
const LOW_CREDITS = 25;

export function lowNotice(account: CreditAccount | null | undefined, model: ModelOptionInfo | undefined): string | undefined {
  if (!account || !model?.tier) return undefined;
  const cost = messageCost(model, account);
  const total = account.credits.total;
  if (!cost || total < cost || total > LOW_CREDITS) return undefined;
  const about = Math.max(Math.round(total / 5) * 5, 5);
  return plural(about, { one: 'About # credit left', other: 'About # credits left' });
}
