// How the Print history and the lines that lead to it are worded. The host
// sends records and counts; every phrase is the page's, in the viewer's own
// time zone. Nothing here says a print failed: a print that stopped early
// simply has no finish (decided 2026-10-27).

import type { HistoryEntry, HistorySummary } from './bridge/protocol';

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
  return when.toLocaleTimeString('en-GB', { hour: '2-digit', minute: '2-digit' });
}

function weekday(when: Date): string {
  return when.toLocaleDateString('en-US', { weekday: 'short' });
}

function monthDay(when: Date, now: Date): string {
  return when.toLocaleDateString('en-US', {
    month: 'short',
    day: 'numeric',
    ...(when.getFullYear() === now.getFullYear() ? {} : { year: 'numeric' }),
  });
}

function plural(count: number, one: string, many = `${one}s`): string {
  return `${count} ${count === 1 ? one : many}`;
}

// "today", "yesterday", "Sun" within the week, else "Oct 20".
export function finishedDay(iso: string, now: Date = new Date()): string {
  const when = new Date(iso);
  if (Number.isNaN(when.getTime())) return '';
  const days = daysAgo(when, now);
  if (days === 0) return 'today';
  if (days === 1) return 'yesterday';
  return days > 1 && days < 7 ? weekday(when) : monthDay(when, now);
}

// A project card's line: "2 prints · 1 printing now", "1 print · finished
// Sun", "Exported once · never printed".
export function projectHistoryLine(history: HistorySummary, now: Date = new Date()): string {
  if (history.prints > 0) {
    const prints = plural(history.prints, 'print');
    if (history.printingNow > 0) return `${prints} · ${history.printingNow} printing now`;
    return history.lastFinishedAt ? `${prints} · finished ${finishedDay(history.lastFinishedAt, now)}` : prints;
  }
  // A print host reports no prints, so a send is not known never to have
  // printed; only a G-code that went nowhere but a file is.
  if (history.sends > 0) return history.sends === 1 ? 'Sent once' : `Sent ${history.sends} times`;
  return `${history.exports === 1 ? 'Exported once' : `Exported ${history.exports} times`} · never printed`;
}

// A printer card's line: "12 prints · 1 printing now", "7 prints · last
// finished Sun".
export function printerHistoryLine(history: HistorySummary, now: Date = new Date()): string {
  if (history.prints > 0) {
    const prints = plural(history.prints, 'print');
    if (history.printingNow > 0) return `${prints} · ${history.printingNow} printing now`;
    return history.lastFinishedAt ? `${prints} · last finished ${finishedDay(history.lastFinishedAt, now)}` : prints;
  }
  if (history.sends > 0) return history.sends === 1 ? 'Sent once' : `Sent ${history.sends} times`;
  return `${plural(history.gcodes, 'G-code')} · never printed`;
}

// The eight leading hex digits in two groups, "a71f 8c04".
export function shortHash(hash: string): string {
  return `${hash.slice(0, 4)} ${hash.slice(4, 8)}`;
}

// The day heading an entry sits under: TODAY, YESTERDAY, SUN within the
// week, else the month, OCT (OCT 2025 outside this year).
export function dayGroup(iso: string, now: Date = new Date()): string {
  const when = new Date(iso);
  if (Number.isNaN(when.getTime())) return '';
  const days = daysAgo(when, now);
  if (days === 0) return 'TODAY';
  if (days === 1) return 'YESTERDAY';
  if (days > 1 && days < 7) return weekday(when).toUpperCase();
  const month = when.toLocaleDateString('en-US', { month: 'short' }).toUpperCase();
  return when.getFullYear() === now.getFullYear() ? month : `${month} ${when.getFullYear()}`;
}

// An entry's own time: "Today 14:22", "Yesterday 19:40", "Sun 10:12",
// "Oct 20 09:02".
export function entryTime(iso: string, now: Date = new Date()): string {
  const when = new Date(iso);
  if (Number.isNaN(when.getTime())) return '';
  const days = daysAgo(when, now);
  const day = days === 0 ? 'Today' : days === 1 ? 'Yesterday' : days > 1 && days < 7 ? weekday(when) : monthDay(when, now);
  return `${day} ${clock(when)}`;
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
  if (minutes === 0) return `${Math.round(seconds)}s`;
  if (minutes < 60) return `${minutes}m`;
  return `${Math.floor(minutes / 60)}h ${String(minutes % 60).padStart(2, '0')}m`;
}

// "2h 34m · 69 g · 181 layers", what the slicer estimated.
export function statsLine(entry: HistoryEntry): string {
  const parts: string[] = [];
  if (entry.printTimeSeconds) parts.push(duration(entry.printTimeSeconds));
  if (entry.grams) parts.push(`${Math.round(entry.grams)} g`);
  if (entry.layers) parts.push(plural(entry.layers, 'layer'));
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
  const counts = `${plural(entries.length, 'G-code')} · ${plural(prints, 'print')}`;
  return filtered ? counts : `${counts} · all projects, all printers`;
}

// Whether an entry holds `needle` anywhere a person would look for it.
export function matches(entry: HistoryEntry, needle: string): boolean {
  if (!needle) return true;
  const text = [
    entry.name,
    entry.projectName,
    entry.plateName,
    entry.hash.slice(0, 8),
    shortHash(entry.hash),
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
