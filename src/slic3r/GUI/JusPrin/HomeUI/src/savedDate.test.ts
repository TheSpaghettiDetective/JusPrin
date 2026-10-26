import { describe, expect, it } from 'vitest';
import { savedDate } from './savedDate';

// Local times throughout, so the day boundaries are the viewer's wherever the
// tests run.
const now = new Date(2026, 9, 26, 9, 30);
const iso = (...parts: [number, number, number, number?, number?]) => new Date(...parts).toISOString();

describe('savedDate', () => {
  it('says Today for any time since midnight', () => {
    expect(savedDate(iso(2026, 9, 26, 0, 1), now)).toBe('Today');
  });

  it('says Yesterday for last night, however late', () => {
    expect(savedDate(iso(2026, 9, 25, 23, 59), now)).toBe('Yesterday');
    expect(savedDate(iso(2026, 9, 25, 0, 0), now)).toBe('Yesterday');
  });

  it('gives the month and day for anything older this year', () => {
    expect(savedDate(iso(2026, 9, 24, 18, 5), now)).toBe('Oct 24');
    expect(savedDate(iso(2026, 0, 3), now)).toBe('Jan 3');
  });

  it('adds the year for another year', () => {
    expect(savedDate(iso(2025, 11, 31, 12), now)).toBe('Dec 31, 2025');
  });

  it('says nothing for a time it cannot read', () => {
    expect(savedDate('', now)).toBe('');
    expect(savedDate('yesterday', now)).toBe('');
  });
});
