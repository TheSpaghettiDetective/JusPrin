// The thread's follow and hold rules (decided 2026-10-28, the canvas's "Chat
// scrolling" board). jsdom lays nothing out, so each test gives the thread a
// height and its column a content height, and tells the page when either
// changed, the way a ResizeObserver would.

import { act, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it } from 'vitest';
import { WireMessage } from '../bridge/protocol';
import { Message } from '../state/store';
import { MessageList } from './MessageList';

const message = (id: string, role: WireMessage['role'], text: string, extra: Partial<WireMessage> = {}): Message =>
  ({ id, role, state: 'complete', text, attempt: 1, lastSeq: -1, ...extra });

const plan = [
  message('m-1', 'user', 'garage wall, bike helmet, 1.5 kg'),
  message('m-2', 'assistant', 'Here is the plan for your garage hook.', { inReplyTo: 'm-1' }),
];

type Observed = { callback: ResizeObserverCallback; targets: Element[] };
let observers: Observed[] = [];
const OriginalResizeObserver = globalThis.ResizeObserver;
const originalMatchMedia = window.matchMedia;

// What layout would measure: the column's height and the thread's.
const layout = { content: 1000, view: 400 };
let top = 0;

beforeEach(() => {
  observers = [];
  layout.content = 1000;
  layout.view = 400;
  top = 0;
  globalThis.ResizeObserver = class {
    private readonly entry: Observed;
    constructor(callback: ResizeObserverCallback) {
      this.entry = { callback, targets: [] };
      observers.push(this.entry);
    }
    observe(target: Element) { this.entry.targets.push(target); }
    unobserve() {}
    disconnect() { this.entry.targets = []; }
  } as unknown as typeof ResizeObserver;
  // Reduced motion: the thread jumps instead of easing, so a test reads
  // where it ends without waiting out an animation.
  window.matchMedia = ((query: string) => ({ matches: query.includes('reduce'), media: query })) as typeof window.matchMedia;
});

afterEach(() => {
  globalThis.ResizeObserver = OriginalResizeObserver;
  window.matchMedia = originalMatchMedia;
});

function renderThread(messages: Message[]) {
  const props = {
    attachments: [], streamingMessageId: null, toolActivities: [], gcodeMarkers: [], changes: [],
    onRetry: () => {}, onToolCancel: () => {}, onSend: () => {},
  };
  const view = render(<MessageList messages={messages} {...props} />);
  const list = screen.getByRole('log', { name: 'Agent conversation' });
  const content = list.querySelector('.message-list-content')!;
  const bottom = () => layout.content - layout.view;
  Object.defineProperty(list, 'scrollHeight', { configurable: true, get: () => layout.content });
  Object.defineProperty(list, 'clientHeight', { configurable: true, get: () => layout.view });
  Object.defineProperty(list, 'scrollTop', {
    configurable: true,
    get: () => top,
    // A browser clamps the position and reports the move as a scroll event.
    set: (value: number) => {
      const next = Math.max(0, Math.min(value, bottom()));
      if (next === top) return;
      top = next;
      queueMicrotask(() => list.dispatchEvent(new Event('scroll')));
    },
  });
  const resized = (target: Element, height: number) =>
    act(() => {
      for (const observer of observers)
        if (observer.targets.includes(target))
          observer.callback([{ target, contentRect: { height } } as unknown as ResizeObserverEntry], {} as ResizeObserver);
    });
  return {
    list,
    bottom,
    // New content in the column: a message, a streamed word, a card.
    grow: (next: Message[], by: number) => {
      view.rerender(<MessageList messages={next} {...props} />);
      layout.content += by;
      resized(content, layout.content);
    },
    // The thread itself gets shorter, as under a composer growing a line.
    shrinkView: (by: number) => {
      layout.view -= by;
      resized(list, layout.view);
    },
    // The person scrolls up to reread.
    scrollUpTo: async (position: number) => {
      top = position;
      fireEvent.wheel(list, { deltaY: -100 });
      fireEvent.scroll(list);
      await act(() => new Promise((resolve) => setTimeout(resolve, 10)));
    },
    opened: () => resized(content, layout.content),
  };
}

const jumpButton = () => screen.queryByRole('button', { name: 'Jump to latest' });
const settle = () => act(() => new Promise((resolve) => setTimeout(resolve, 50)));

describe('thread scrolling', () => {
  it('opens at the bottom and follows answers and app turns while the person is there', async () => {
    const thread = renderThread(plan);
    thread.opened();
    await waitFor(() => expect(thread.list.scrollTop).toBeGreaterThanOrEqual(thread.bottom() - 1));

    thread.grow([...plan, message('n-1', 'note', 'Slice finished')], 300);
    await waitFor(() => expect(thread.list.scrollTop).toBeGreaterThanOrEqual(thread.bottom() - 1));
    expect(jumpButton()).not.toBeInTheDocument();
  });

  it('holds still while the person reads above, and shows the jump button', async () => {
    const thread = renderThread(plan);
    thread.opened();
    await waitFor(() => expect(thread.list.scrollTop).toBeGreaterThanOrEqual(thread.bottom() - 1));

    await thread.scrollUpTo(100);
    await waitFor(() => expect(jumpButton()).toBeInTheDocument());

    // An answer streaming in and the app's own turn do not move the view.
    thread.grow([...plan, message('m-3', 'assistant', 'The slice finished.', { inReplyTo: 'm-1' })], 400);
    await settle();
    expect(thread.list.scrollTop).toBe(100);
    expect(jumpButton()).toBeInTheDocument();
  });

  it("brings the person's own message into view and follows its answer, wherever they had scrolled", async () => {
    const thread = renderThread(plan);
    thread.opened();
    await waitFor(() => expect(thread.list.scrollTop).toBeGreaterThanOrEqual(thread.bottom() - 1));
    await thread.scrollUpTo(0);
    await waitFor(() => expect(jumpButton()).toBeInTheDocument());

    const asked = [...plan, message('m-3', 'user', 'Is it still fine to print like this?')];
    thread.grow(asked, 60);
    await waitFor(() => expect(thread.list.scrollTop).toBeGreaterThanOrEqual(thread.bottom() - 1));
    await waitFor(() => expect(jumpButton()).not.toBeInTheDocument());

    thread.grow([...asked, message('m-4', 'assistant', 'The catch is your printer.', { inReplyTo: 'm-3' })], 500);
    await waitFor(() => expect(thread.list.scrollTop).toBeGreaterThanOrEqual(thread.bottom() - 1));
  });

  it('keeps the bottom in view when the thread gets shorter under the composer', async () => {
    const thread = renderThread(plan);
    thread.opened();
    await waitFor(() => expect(thread.list.scrollTop).toBeGreaterThanOrEqual(thread.bottom() - 1));
    // Long after the thread opened: nothing is still scrolling it.
    await settle();

    thread.shrinkView(100);
    await waitFor(() => expect(thread.list.scrollTop).toBeGreaterThanOrEqual(thread.bottom() - 1));
    expect(jumpButton()).not.toBeInTheDocument();
  });

  it('jumps to the bottom from the button and follows again', async () => {
    const thread = renderThread(plan);
    thread.opened();
    await waitFor(() => expect(thread.list.scrollTop).toBeGreaterThanOrEqual(thread.bottom() - 1));
    await thread.scrollUpTo(50);
    await waitFor(() => expect(jumpButton()).toBeInTheDocument());

    fireEvent.click(jumpButton()!);
    await waitFor(() => expect(thread.list.scrollTop).toBeGreaterThanOrEqual(thread.bottom() - 1));
    await waitFor(() => expect(jumpButton()).not.toBeInTheDocument());

    thread.grow([...plan, message('m-3', 'assistant', 'More.', { inReplyTo: 'm-1' })], 300);
    await waitFor(() => expect(thread.list.scrollTop).toBeGreaterThanOrEqual(thread.bottom() - 1));
  });
});

