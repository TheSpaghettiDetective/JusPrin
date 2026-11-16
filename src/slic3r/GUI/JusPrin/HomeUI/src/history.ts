// How the Print history and the lines that lead to it are worded. The host
// sends records and counts; every phrase is the page's, in the viewer's own
// time zone. Nothing here says a print failed: a print that stopped early
// simply has no finish (decided 2026-10-27).

import { plural, t } from '@lingui/core/macro';
import type { HistoryEntry, HistorySummary } from './bridge/protocol';
import { decimalText } from '../../AgentUI/src/i18n';
import { dateLocale } from './savedDate';

const DAY = 86_400_000;

function midnight(date: Date): number {
  return new Date(date.getFullYear(), date.getMonth(), date.getDate()).getTime();
}

// Whole days from `when` to `now`, by the calendar: a day that changes the
// clock is 23 or 25 hours long, so the difference is rounded.
function daysAgo(when: Date, now: Date): number {
  return Math.round((midnight(now) - midnight(when)) / DAY);
}

function clock(when: Date): string {
  return when.toLocaleTimeString(dateLocale('en-GB'), { hour: '2-digit', minute: '2-digit' });
}

function weekday(when: Date): string {
  return when.toLocaleDateString(dateLocale('en-US'), { weekday: 'short' });
}

function monthDay(when: Date, now: Date): string {
  return when.toLocaleDateString(dateLocale('en-US'), {
    month: 'short',
    day: 'numeric',
    ...(when.getFullYear() === now.getFullYear() ? {} : { year: 'numeric' }),
  });
}

// The counts the lines share. The number is written as the code writes it, so
// a count over a thousand reads 1200, as it always has.
function printCount(count: number): string {
  return plural(count, { one: `${count} print`, other: `${count} prints` });
}

function gcodeCount(count: number): string {
  return plural(count, { one: `${count} G-code`, other: `${count} G-codes` });
}

function printingNowCount(count: number): string {
  return plural(count, { one: `${count} printing now`, other: `${count} printing now` });
}

// "finished today", "finished yesterday", "finished Sun" within the week,
// else "finished Oct 20"; `last` says "last finished …", as a printer card does.
function finishedLine(iso: string, now: Date, last: boolean): string {
  const when = new Date(iso);
  const days = Number.isNaN(when.getTime()) ? undefined : daysAgo(when, now);
  if (days === 0) return last ? t`last finished today` : t`finished today`;
  if (days === 1) return last ? t`last finished yesterday` : t`finished yesterday`;
  const day = days === undefined ? '' : days > 1 && days < 7 ? weekday(when) : monthDay(when, now);
  return last
    ? t({ message: `last finished ${day}`, comment: '{day} is a short weekday (Sun) or a date (Oct 20)' })
    : t({ message: `finished ${day}`, comment: '{day} is a short weekday (Sun) or a date (Oct 20)' });
}

// "Sent once", "Sent 3 times".
function sentLine(sends: number): string {
  return plural(sends, { 1: 'Sent once', one: `Sent ${sends} times`, other: `Sent ${sends} times` });
}

// A project card's line: "2 prints · 1 printing now", "1 print · finished
// Sun", "Exported once, never sent to a printer".
export function projectHistoryLine(history: HistorySummary, now: Date = new Date()): string {
  if (history.prints > 0) {
    const prints = printCount(history.prints);
    if (history.printingNow > 0) return `${prints} · ${printingNowCount(history.printingNow)}`;
    return history.lastFinishedAt ? `${prints} · ${finishedLine(history.lastFinishedAt, now, false)}` : prints;
  }
  // A print host reports no prints, so a send is not known never to have
  // printed; only a G-code that went nowhere but a file is.
  if (history.sends > 0) return sentLine(history.sends);
  const exports = history.exports;
  return plural(exports, {
    1: 'Exported once, never sent to a printer',
    one: `Exported ${exports} times, never sent to a printer`,
    other: `Exported ${exports} times, never sent to a printer`,
  });
}

// What a print report earns, in the words of the button and the card's line.
// The gateway's JUSPRIN_V2_REPORT_CREDITS decides what is granted; it has no way
// to tell this page before a report is sent.
export const REPORT_CREDITS = 25;
const reportCredits = REPORT_CREDITS;
export const REPORT_OFFER = plural(reportCredits, {
  one: `Share how it printed and get ${reportCredits} credit`,
  other: `Share how it printed and get ${reportCredits} credits`,
});
export const REPORT_MIN_TEXT = 200;

// "Plate 1 → Bambu X1C": what the report form is about. A G-code that only
// went to a file names no printer. The hash stays internal.
export function reportSubject(entry: HistoryEntry): string {
  const sends = entry.destinations.filter((row) => row.kind === 'send');
  const printer = entry.prints[entry.prints.length - 1]?.printerName ?? sends[sends.length - 1]?.printerName;
  return `${entry.name}${printer ? ` → ${printer}` : ''}`;
}

export function reportSentDetail(report: NonNullable<HistoryEntry['report']>, createdAt: string): string {
  const when = rowTime(report.sentAt, createdAt);
  const credits = report.credits;
  return credits > 0 ? `${when} · ${plural(credits, { one: `${credits} credit added`, other: `${credits} credits added` })}` : when;
}

export function fileSize(bytes: number): string {
  const megabytes = decimalText(bytes / 1_000_000, 1, 1);
  const kilobytes = Math.max(1, Math.round(bytes / 1_000));
  return bytes >= 1_000_000 ? t`${megabytes} MB` : t`${kilobytes} KB`;
}

// A printer card's line: "12 prints · 1 printing now", "7 prints · last
// finished Sun", "1 G-code, never sent to a printer".
export function printerHistoryLine(history: HistorySummary, now: Date = new Date()): string {
  if (history.prints > 0) {
    const prints = printCount(history.prints);
    if (history.printingNow > 0) return `${prints} · ${printingNowCount(history.printingNow)}`;
    return history.lastFinishedAt ? `${prints} · ${finishedLine(history.lastFinishedAt, now, true)}` : prints;
  }
  if (history.sends > 0) return sentLine(history.sends);
  const gcodes = history.gcodes;
  return plural(gcodes, {
    one: `${gcodes} G-code, never sent to a printer`,
    other: `${gcodes} G-codes, never sent to a printer`,
  });
}

// The day heading an entry sits under: TODAY, YESTERDAY, SUN within the
// week, else the month, OCT (OCT 2025 outside this year).
export function dayGroup(iso: string, now: Date = new Date()): string {
  const when = new Date(iso);
  if (Number.isNaN(when.getTime())) return '';
  const days = daysAgo(when, now);
  const locale = dateLocale('en-US');
  if (days === 0) return t`Today`.toLocaleUpperCase(locale);
  if (days === 1) return t`Yesterday`.toLocaleUpperCase(locale);
  if (days > 1 && days < 7) return weekday(when).toLocaleUpperCase(locale);
  return when.toLocaleDateString(locale, {
    month: 'short',
    ...(when.getFullYear() === now.getFullYear() ? {} : { year: 'numeric' }),
  }).toLocaleUpperCase(locale);
}

// An entry's own time: "Today 14:22", "Yesterday 19:40", "Sun 10:12",
// "Oct 20 09:02".
export function entryTime(iso: string, now: Date = new Date()): string {
  const when = new Date(iso);
  if (Number.isNaN(when.getTime())) return '';
  const days = daysAgo(when, now);
  const time = clock(when);
  if (days === 0) return t({ message: `Today ${time}`, comment: '{time} is a clock time, 14:22' });
  if (days === 1) return t({ message: `Yesterday ${time}`, comment: '{time} is a clock time, 19:40' });
  return `${days > 1 && days < 7 ? weekday(when) : monthDay(when, now)} ${time}`;
}

// A row's time: the clock alone on the entry's own day, else with the day.
export function rowTime(iso: string, entryIso: string, now: Date = new Date()): string {
  const when = new Date(iso);
  if (Number.isNaN(when.getTime())) return '';
  const entry = new Date(entryIso);
  return !Number.isNaN(entry.getTime()) && midnight(entry) === midnight(when) ? clock(when) : entryTime(iso, now);
}

// "13:09 → 15:41", the end with its day when the print ran past midnight.
export function printSpan(startedIso: string, finishedIso: string | undefined, entryIso: string, now: Date = new Date()): string {
  const start = rowTime(startedIso, entryIso, now);
  if (!finishedIso) return `${start} →`;
  return `${start} → ${rowTime(finishedIso, startedIso, now)}`;
}

// "2h 34m", "34m", or "45s" under a minute.
export function duration(seconds: number): string {
  const minutes = Math.round(seconds / 60);
  if (minutes === 0) {
    const secs = Math.round(seconds);
    return t({ message: `${secs}s`, comment: 'A duration in seconds, abbreviated: 45s' });
  }
  if (minutes < 60) return t({ message: `${minutes}m`, comment: 'A duration in minutes, abbreviated: 34m' });
  const hours = Math.floor(minutes / 60);
  const mins = String(minutes % 60).padStart(2, '0');
  return t({ message: `${hours}h ${mins}m`, comment: 'A duration in hours and minutes, abbreviated: 2h 04m' });
}

// "2h 34m · 69 g · 181 layers", what the slicer estimated.
export function statsLine(entry: HistoryEntry): string {
  const parts: string[] = [];
  if (entry.printTimeSeconds) parts.push(duration(entry.printTimeSeconds));
  const grams = entry.grams ? Math.round(entry.grams) : 0;
  if (entry.grams) parts.push(t({ message: `${grams} g`, comment: 'Grams of filament' }));
  const layers = entry.layers ?? 0;
  if (layers) parts.push(plural(layers, { one: `${layers} layer`, other: `${layers} layers` }));
  return parts.join(' · ');
}

// The folder an export went to, as its last two parts: "Desktop / Prints",
// or the drive alone, "E:".
export function folderOf(path: string): string {
  const parts = path.split(/[\\/]/).filter(Boolean);
  parts.pop();
  return parts.slice(-2).join(' / ');
}

export function fileName(path: string): string {
  return path.split(/[\\/]/).filter(Boolean).pop() ?? path;
}

// "5 G-codes · 3 prints", and whose they are when nothing narrows them.
export function countLine(entries: HistoryEntry[], filtered: boolean): string {
  const prints = entries.reduce((total, entry) => total + entry.prints.length, 0);
  const counts = `${gcodeCount(entries.length)} · ${printCount(prints)}`;
  return filtered ? counts : `${counts} · ${t`all projects, all printers`}`;
}

// Whether an entry holds `needle` anywhere a person would look for it.
export function matches(entry: HistoryEntry, needle: string): boolean {
  if (!needle) return true;
  const text = [
    entry.name,
    entry.projectName,
    entry.plateName,
    entry.printer,
    entry.material,
    entry.process,
    ...entry.destinations.map((row) => (row.kind === 'export' ? row.path : row.printerName)),
    ...entry.prints.map((print) => print.printerName),
  ]
    .filter(Boolean)
    .join('\n')
    .toLocaleLowerCase();
  return text.includes(needle.toLocaleLowerCase());
}
