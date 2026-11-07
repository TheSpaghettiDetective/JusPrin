// The printer card: one status, then each field only when the host sent it,
// so the card grows and shrinks with what the printer reports.

import { describe, expect, it, vi } from 'vitest';
import { act, fireEvent, render, screen } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import type { PrinterInfo } from '../bridge/protocol';
import { CAMERA_CHECK_EVERY_MS, lastConnected, PrinterCard, statusDetail, timeLeft } from './PrinterCard';

function printer(overrides: Partial<PrinterInfo> = {}): PrinterInfo {
  return {
    id: 'named:Bench',
    name: 'Bench',
    kind: 'named',
    canOpenSettings: true,
    canRename: true,
    canRemove: true,
    status: 'ready',
    modelText: 'X1 Carbon · 0.4 mm',
    spools: [],
    action: 'none',
    ...overrides,
  };
}

const NOW = new Date('2026-11-05T15:00:00Z');

function card(info: PrinterInfo, onLaunchMonitor = vi.fn(), onConnect = vi.fn(), onHistory = vi.fn()) {
  const actions = { onConnect, onOpenSettings: vi.fn(), onAskAboutSettings: vi.fn(), onRename: vi.fn(), onRemove: vi.fn() };
  render(
    <PrinterCard
      printer={info}
      otherNames={[]}
      actions={actions}
      onLaunchMonitor={onLaunchMonitor}
      onHistory={onHistory}
      now={NOW}
    />,
  );
  return document.querySelector('.printer-card') as HTMLElement;
}

// The card's parts in order, by class, under the card itself.
function parts(element: HTMLElement): string[] {
  return [...element.children].map((child) => child.className.split(' ')[0]);
}

describe('the status line', () => {
  it.each([
    ['printing', 'Printing', 'printing'],
    ['ready', 'Ready', 'ready'],
    ['paused', 'Paused', 'paused'],
    ['attention', 'Needs attention', 'attention'],
    ['connected', 'Connected', 'connected'],
  ] as const)('says %s with its own dot', (status, word, dot) => {
    const element = card(printer({ status }));
    expect(element.querySelector('.printer-status-word')).toHaveTextContent(word);
    expect(element.querySelector('.printer-status .status-dot')).toHaveClass(dot);
  });

  it.each([
    ['not_connected', 'Not connected'],
    ['connecting', 'Connecting…'],
  ] as const)('says %s in words with no dot', (status, word) => {
    const element = card(printer({ status }));
    expect(element.querySelector('.printer-status-word')).toHaveTextContent(word);
    expect(element.querySelector('.printer-status .status-dot')).toBeNull();
  });

  it('follows the word with how far a job is, or how the last one ended', () => {
    expect(statusDetail(printer({ status: 'printing', progressPercent: 43, secondsLeft: 8040 }))).toBe('43% · 2h 14m left');
    expect(statusDetail(printer({ status: 'printing', progressPercent: 61 }))).toBe('61%');
    expect(statusDetail(printer({ status: 'printing' }))).toBeUndefined();
    expect(statusDetail(printer({ status: 'paused', progressPercent: 27 }))).toBe('at 27%');
    expect(statusDetail(printer({ status: 'attention', progressPercent: 40 }))).toBe('at 40%');
    expect(statusDetail(printer({ status: 'ready', lastPrint: 'finished' }))).toBe('last print finished');
    expect(statusDetail(printer({ status: 'ready' }))).toBeUndefined();
    // A printer that is not connected has no job to report on.
    expect(statusDetail(printer({ status: 'not_connected', progressPercent: 27 }))).toBeUndefined();
  });

  it('words the time left', () => {
    expect(timeLeft(8040)).toBe('2h 14m left');
    expect(timeLeft(840)).toBe('14m left');
    expect(timeLeft(20)).toBe('1m left');
  });
});

describe('the fields', () => {
  it('shows only the name, the status and the facts when that is all there is', () => {
    const element = card(printer());
    expect(parts(element)).toEqual(['printer-head', 'printer-status', 'printer-facts']);
  });

  it('keeps the boards order when every field has a value', () => {
    const element = card(
      printer({
        status: 'paused',
        job: 'Phone stand',
        progressPercent: 27,
        message: 'AMS A Slot 1 filament has run out. Please insert a new filament.',
        camera: { streamUrl: 'http://127.0.0.1:7140/api/stream.mjpeg?src=klipper' },
        connectionLabel: 'Cloud',
        spools: [{ material: 'PLA', colour: '#262626' }],
        history: { gcodes: 9, exports: 0, sends: 9, prints: 9, printingNow: 1 },
        action: 'monitor',
      }),
    );
    expect(parts(element)).toEqual([
      'printer-camera',
      'printer-head',
      'printer-status',
      'printer-job',
      'printer-progress',
      'printer-message',
      'printer-facts',
      'printer-spools',
      'project-history',
      'button-secondary',
    ]);
  });

  it('shows the job and its bar while a job runs, and not on a ready printer', () => {
    card(printer({ status: 'printing', job: 'Vent grille', progressPercent: 43 }));
    expect(screen.getByText('Vent grille')).toBeInTheDocument();
    expect(screen.getByRole('progressbar')).toHaveAttribute('aria-valuenow', '43');
    document.body.innerHTML = '';
    card(printer({ status: 'ready', job: 'Vent grille', progressPercent: 100 }));
    expect(screen.queryByText('Vent grille')).toBeNull();
    expect(screen.queryByRole('progressbar')).toBeNull();
  });

  it('joins how it is reached, its address, model and nozzle on one line', () => {
    const element = card(
      printer({ connectionLabel: 'Klipper', address: 'voron.local', modelText: 'Voron 2.4 · 0.4 mm' }),
    );
    expect(element.querySelector('.printer-facts')).toHaveTextContent('Klipper · voron.local · Voron 2.4 · 0.4 mm');
  });

  it('says when a printer that is not connected was last heard from, and nothing when unknown', () => {
    card(
      printer({
        status: 'not_connected',
        message: "Klipper stopped: Lost communication with MCU 'mcu'",
        lastConnectedAt: '2026-11-05T12:00:00Z',
      }),
    );
    expect(screen.getByText("Klipper stopped: Lost communication with MCU 'mcu'")).toBeInTheDocument();
    expect(screen.getByText('Last connected 3h ago')).toBeInTheDocument();
    document.body.innerHTML = '';
    card(printer({ status: 'not_connected', message: 'The printer stopped sending updates.' }));
    expect(screen.queryByText(/Last connected/)).toBeNull();
  });

  it('words how long ago', () => {
    expect(lastConnected('2026-11-05T14:59:40Z', NOW)).toBe('Last connected just now');
    expect(lastConnected('2026-11-05T14:40:00Z', NOW)).toBe('Last connected 20 min ago');
    expect(lastConnected('2026-11-04T15:00:00Z', NOW)).toBe('Last connected 1 day ago');
    expect(lastConnected('2026-11-03T14:00:00Z', NOW)).toBe('Last connected 2 days ago');
    expect(lastConnected('not a date', NOW)).toBeUndefined();
  });

  it('names the materials once and draws a swatch per coloured spool', () => {
    const element = card(
      printer({
        spools: [
          { material: 'PLA', colour: '#9A9A9A' },
          { material: 'PETG', colour: '#C8202D' },
          { material: 'PLA', colour: '#FFFFFF' },
          { material: 'PLA' },
        ],
      }),
    );
    const loaded = element.querySelector('.printer-spools')!;
    expect(loaded).toHaveTextContent('PLA, PETG');
    expect(loaded.querySelectorAll('.spool-swatch')).toHaveLength(3);
  });
});

describe('the camera', () => {
  it('shows a stream on any status, as long as it answers', () => {
    const element = card(printer({ status: 'ready', camera: { streamUrl: 'http://cam/stream' } }));
    const image = screen.getByRole('img', { name: 'Camera view of Bench' });
    expect(image).toHaveAttribute('src', 'http://cam/stream');
    fireEvent.error(image);
    expect(element.querySelector('.printer-camera')).toBeNull();
  });

  it('asks a camera with only a picture for the next one every two seconds', () => {
    vi.useFakeTimers();
    try {
      card(printer({ status: 'printing', camera: { snapshotUrl: 'http://cam/frame.jpeg?src=klipper' } }));
      const image = screen.getByRole('img', { name: 'Camera view of Bench' });
      expect(image).toHaveAttribute('src', 'http://cam/frame.jpeg?src=klipper&t=0');
      act(() => {
        vi.advanceTimersByTime(2000);
      });
      expect(image).toHaveAttribute('src', 'http://cam/frame.jpeg?src=klipper&t=1');
    } finally {
      vi.useRealTimers();
    }
  });

  // The pictures the card asks for out of sight, to learn whether the camera
  // still answers; the test settles each one.
  function watchChecks(): { onload: (() => void) | null; onerror: (() => void) | null; src: string }[] {
    const asked: { onload: (() => void) | null; onerror: (() => void) | null; src: string }[] = [];
    vi.stubGlobal(
      'Image',
      class {
        onload: (() => void) | null = null;
        onerror: (() => void) | null = null;
        src = '';
        constructor() {
          asked.push(this);
        }
      },
    );
    return asked;
  }

  it('takes off a stream that stops answering, and reconnects it once the camera answers again', () => {
    vi.useFakeTimers();
    const asked = watchChecks();
    try {
      const element = card(
        printer({ status: 'printing', camera: { streamUrl: 'http://cam/stream', snapshotUrl: 'http://cam/frame.jpeg' } }),
      );
      const first = screen.getByRole('img', { name: 'Camera view of Bench' });
      act(() => {
        vi.advanceTimersByTime(CAMERA_CHECK_EVERY_MS);
      });
      expect(asked[0].src).toBe('http://cam/frame.jpeg?t=0');
      act(() => asked[0].onload?.());
      expect(screen.getByRole('img', { name: 'Camera view of Bench' })).toBe(first);

      // The stream's image keeps its last frame; the check is what notices.
      act(() => {
        vi.advanceTimersByTime(CAMERA_CHECK_EVERY_MS);
      });
      act(() => asked[1].onerror?.());
      expect(element.querySelector('.printer-camera')).toBeNull();

      act(() => {
        vi.advanceTimersByTime(CAMERA_CHECK_EVERY_MS);
      });
      act(() => asked[2].onload?.());
      const again = screen.getByRole('img', { name: 'Camera view of Bench' });
      expect(again).toHaveAttribute('src', 'http://cam/stream');
      expect(again).not.toBe(first);
    } finally {
      vi.unstubAllGlobals();
      vi.useRealTimers();
    }
  });

  it('takes off a camera whose check is still unanswered at the next one', () => {
    vi.useFakeTimers();
    const asked = watchChecks();
    try {
      const element = card(printer({ camera: { streamUrl: 'http://cam/stream' } }));
      act(() => {
        vi.advanceTimersByTime(CAMERA_CHECK_EVERY_MS);
      });
      // With no picture to ask for, the check asks for the stream itself.
      expect(asked[0].src).toBe('http://cam/stream?t=0');
      act(() => {
        vi.advanceTimersByTime(CAMERA_CHECK_EVERY_MS);
      });
      expect(element.querySelector('.printer-camera')).toBeNull();
      // The hanging check was let go.
      expect(asked[0].src).toBe('');
    } finally {
      vi.unstubAllGlobals();
      vi.useRealTimers();
    }
  });
});

describe('the button', () => {
  it.each([
    ['monitor', 'Launch monitor'],
    ['printer_window', 'Open printer window'],
  ] as const)('%s opens the printer', async (action, label) => {
    const launch = vi.fn();
    card(printer({ action }), launch);
    await userEvent.click(screen.getByRole('button', { name: label }));
    expect(launch).toHaveBeenCalledWith('named:Bench');
  });

  it.each([
    ['reconnect', 'Reconnect'],
    ['connect', 'Connect…'],
  ] as const)('%s asks to connect', async (action, label) => {
    const connect = vi.fn();
    card(printer({ status: 'not_connected', action }), vi.fn(), connect);
    await userEvent.click(screen.getByRole('button', { name: label }));
    expect(connect).toHaveBeenCalledWith('named:Bench');
  });

  it('is left out when there is nothing to do', () => {
    card(printer({ status: 'connecting', action: 'none' }));
    expect(screen.queryByRole('button', { name: /monitor|window|connect/i })).toBeNull();
  });
});
