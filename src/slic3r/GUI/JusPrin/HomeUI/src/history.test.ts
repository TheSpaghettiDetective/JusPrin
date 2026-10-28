import { describe, expect, it } from 'vitest';
import type { HistoryEntry, HistorySummary } from './bridge/protocol';
import {
  countLine,
  dayGroup,
  duration,
  entryTime,
  folderOf,
  matches,
  printerHistoryLine,
  printSpan,
  projectHistoryLine,
  rowTime,
  shortHash,
  statsLine,
} from './history';

// Local times throughout, so the day boundaries are the viewer's wherever the
// tests run. Oct 28 2026 is a Wednesday.
const now = new Date(2026, 9, 28, 16, 0);
const iso = (...parts: [number, number, number, number?, number?]) => new Date(...parts).toISOString();

function summary(overrides: Partial<HistorySummary> = {}): HistorySummary {
  return { gcodes: 1, exports: 0, sends: 0, prints: 0, printingNow: 0, ...overrides };
}

function entry(overrides: Partial<HistoryEntry> = {}): HistoryEntry {
  return {
    projectId: 'p1',
    projectName: 'Garage bracket',
    hash: 'a71f8c04deadbeef',
    name: 'Plate 1',
    plateName: 'Plate 1',
    createdAt: iso(2026, 9, 28, 13, 4),
    kept: true,
    printAgain: 'ready',
    printerIds: ['named:Bambu X1C'],
    destinations: [],
    prints: [],
    ...overrides,
  };
}

describe('the card lines', () => {
  it('count prints and say what is printing now', () => {
    expect(projectHistoryLine(summary({ prints: 2, printingNow: 1 }), now)).toBe('2 prints · 1 printing now');
    expect(printerHistoryLine(summary({ prints: 12, printingNow: 1 }), now)).toBe('12 prints · 1 printing now');
  });

  it('say when the last print finished', () => {
    const sunday = iso(2026, 9, 25, 14, 21);
    expect(projectHistoryLine(summary({ prints: 1, lastFinishedAt: sunday }), now)).toBe('1 print · finished Sun');
    expect(printerHistoryLine(summary({ prints: 7, lastFinishedAt: sunday }), now)).toBe('7 prints · last finished Sun');
    expect(projectHistoryLine(summary({ prints: 1, lastFinishedAt: iso(2026, 9, 28, 9) }), now)).toBe('1 print · finished today');
  });

  // A print that never finished is still a print; it is neither failed nor
  // finished, and the line says only how many.
  it('count a print that stopped early without calling it anything', () => {
    expect(projectHistoryLine(summary({ prints: 1 }), now)).toBe('1 print');
  });

  it('say an exported G-code never printed, but not a sent one', () => {
    expect(projectHistoryLine(summary({ exports: 1 }), now)).toBe('Exported once · never printed');
    expect(projectHistoryLine(summary({ exports: 3 }), now)).toBe('Exported 3 times · never printed');
    // A print host reports no prints, so a send may have printed.
    expect(projectHistoryLine(summary({ sends: 2 }), now)).toBe('Sent 2 times');
    expect(printerHistoryLine(summary({ gcodes: 2 }), now)).toBe('2 G-codes · never printed');
  });
});

describe('the screen wording', () => {
  it('groups by day, then by month', () => {
    expect(dayGroup(iso(2026, 9, 28, 0, 5), now)).toBe('TODAY');
    expect(dayGroup(iso(2026, 9, 27, 19, 40), now)).toBe('YESTERDAY');
    expect(dayGroup(iso(2026, 9, 25, 10, 12), now)).toBe('SUN');
    expect(dayGroup(iso(2026, 9, 20, 9, 2), now)).toBe('OCT');
    expect(dayGroup(iso(2025, 11, 2, 9, 2), now)).toBe('DEC 2025');
  });

  it('times an entry with its day', () => {
    expect(entryTime(iso(2026, 9, 28, 14, 22), now)).toBe('Today 14:22');
    expect(entryTime(iso(2026, 9, 27, 19, 40), now)).toBe('Yesterday 19:40');
    expect(entryTime(iso(2026, 9, 25, 10, 12), now)).toBe('Sun 10:12');
    expect(entryTime(iso(2026, 9, 20, 9, 2), now)).toBe('Oct 20 09:02');
  });

  it('times a row by the clock on the entry day, and a print from start to finish', () => {
    const created = iso(2026, 9, 25, 10, 12);
    expect(rowTime(iso(2026, 9, 25, 10, 14), created, now)).toBe('10:14');
    expect(rowTime(iso(2026, 9, 27, 8, 0), created, now)).toBe('Yesterday 08:00');
    expect(printSpan(iso(2026, 9, 25, 10, 14), iso(2026, 9, 25, 14, 21), created, now)).toBe('10:14 → 14:21');
    expect(printSpan(iso(2026, 9, 25, 10, 14), undefined, created, now)).toBe('10:14 →');
  });

  it('writes the hash in two groups and the estimates on one line', () => {
    expect(shortHash('a71f8c04deadbeef')).toBe('a71f 8c04');
    expect(statsLine(entry({ printTimeSeconds: 9240, grams: 68.6, layers: 181 }))).toBe('2h 34m · 69 g · 181 layers');
    expect(statsLine(entry())).toBe('');
    expect(duration(3900)).toBe('1h 05m');
    expect(duration(5700)).toBe('1h 35m');
  });

  it('names the folder an export went to by its last two parts', () => {
    expect(folderOf('C:\\Users\\kj\\Desktop\\Prints\\bracket-v3.gcode')).toBe('Desktop / Prints');
    expect(folderOf('/Users/kj/Desktop/Prints/bracket-v3.gcode')).toBe('Desktop / Prints');
    expect(folderOf('E:\\bracket-v3.gcode')).toBe('E:');
  });

  it('counts what is shown, and says whose when nothing narrows it', () => {
    const printed = entry({ prints: [{ id: 'p-1', printerName: 'Bambu X1C', startedAt: iso(2026, 9, 28, 13, 9) }] });
    expect(countLine([printed, entry()], false)).toBe('2 G-codes · 1 print · all projects, all printers');
    expect(countLine([printed], true)).toBe('1 G-code · 1 print');
  });

  it('finds an entry by its name, project, hash, printer or file', () => {
    const sent = entry({
      destinations: [{ id: 'd-1', kind: 'send', at: iso(2026, 9, 28, 13, 6), printerName: 'Bambu X1C', delivered: true, startRequested: true }],
    });
    expect(matches(sent, 'garage')).toBe(true);
    expect(matches(sent, 'a71f 8c04')).toBe(true);
    expect(matches(sent, 'x1c')).toBe(true);
    expect(matches(sent, 'vent')).toBe(false);
  });
});
