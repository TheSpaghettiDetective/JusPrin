import { ComponentProps } from 'react';
import { act, fireEvent, render, screen } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import tokens from '@resources/jusprin/ui/design-tokens.json';
import { AttachmentInfo } from '../bridge/protocol';
import { applyStaticTokens, composerInputMaxHeight } from '../tokens';
import { ATTACHMENT_CHOICES, Composer } from './Composer';

type Props = ComponentProps<typeof Composer>;

function renderComposer(overrides: Partial<Props> = {}) {
  const props: Props = {
    disabled: false,
    streaming: false,
    attachments: [],
    onSend: vi.fn(),
    onStop: vi.fn(),
    onAttachFiles: vi.fn(),
    onRemoveAttachment: vi.fn(),
    ...overrides,
  };
  const view = render(<Composer {...props} />);
  return { ...view, props, field: screen.getByRole('textbox', { name: 'Message the Agent' }) as HTMLTextAreaElement };
}

const staged: AttachmentInfo = {
  id: 'a-1', name: 'bracket-v3.3mf', kind: 'model', mime: 'model/3mf', sizeBytes: 2516582, source: 'picker', state: 'staged',
};

// jsdom does no layout, so a field's scrollHeight is always 0. These tests
// stand in for the browser: one Body Small line per line of text, plus the
// field's inset above and below.
const { lineHeight } = tokens.typography.roles.bodySmall;
const { inputPadding, maxLines } = tokens.component.composer;
function measureLines() {
  Object.defineProperty(HTMLTextAreaElement.prototype, 'scrollHeight', {
    configurable: true,
    get(this: HTMLTextAreaElement) { return Math.max(1, this.value.split('\n').length) * lineHeight + 2 * inputPadding; },
  });
}
function stopMeasuring() {
  delete (HTMLTextAreaElement.prototype as { scrollHeight?: number }).scrollHeight;
}

afterEach(() => { vi.restoreAllMocks(); stopMeasuring(); });

describe('the message field grows with its text', () => {
  beforeEach(() => { measureLines(); });

  it('starts one line high', () => {
    const { field } = renderComposer();
    expect(field.style.height).toBe(`${lineHeight + 2 * inputPadding}px`);
  });

  it('grows a line at a time', async () => {
    const { field } = renderComposer();
    await userEvent.type(field, 'one{Shift>}{Enter}{/Shift}two{Shift>}{Enter}{/Shift}three');
    expect(field.value).toBe('one\ntwo\nthree');
    expect(field.style.height).toBe(`${3 * lineHeight + 2 * inputPadding}px`);
  });

  it(`stops at ${maxLines} lines and leaves the rest to scroll`, () => {
    const { field } = renderComposer();
    fireEvent.change(field, { target: { value: Array.from({ length: 20 }, (_, i) => `line ${i}`).join('\n') } });
    expect(field.style.height).toBe(`${composerInputMaxHeight()}px`);
    expect(composerInputMaxHeight()).toBe(maxLines * lineHeight + 2 * inputPadding);
  });

  it('shrinks again when the text gets shorter', () => {
    const { field } = renderComposer();
    fireEvent.change(field, { target: { value: 'a\nb\nc\nd' } });
    fireEvent.change(field, { target: { value: 'a' } });
    expect(field.style.height).toBe(`${lineHeight + 2 * inputPadding}px`);
  });

  it('keeps its height when it is not laid out', () => {
    stopMeasuring();
    const { field } = renderComposer();
    fireEvent.change(field, { target: { value: 'a\nb' } });
    expect(field.style.height).toBe('');
  });
});

describe('the box tokens', () => {
  it('writes the box insets from component.composer', () => {
    applyStaticTokens();
    const style = document.documentElement.style;
    expect(style.getPropertyValue('--composer-padding')).toBe(`${tokens.component.composer.padding}px`);
    expect(style.getPropertyValue('--composer-gap')).toBe(`${tokens.component.composer.gap}px`);
    expect(style.getPropertyValue('--composer-input-padding')).toBe(`${inputPadding}px`);
  });
});

describe('Enter and Shift+Enter', () => {
  it('sends on Enter and clears the field', async () => {
    const { field, props } = renderComposer();
    await userEvent.type(field, 'Make it stronger{Enter}');
    expect(props.onSend).toHaveBeenCalledWith('Make it stronger');
    expect(field.value).toBe('');
  });

  it('adds a line on Shift+Enter without sending', async () => {
    const { field, props } = renderComposer();
    await userEvent.type(field, 'first{Shift>}{Enter}{/Shift}second');
    expect(props.onSend).not.toHaveBeenCalled();
    expect(field.value).toBe('first\nsecond');
  });

  it('does not send while an input method is composing', () => {
    const { field, props } = renderComposer();
    fireEvent.change(field, { target: { value: 'にほん' } });
    fireEvent.keyDown(field, { key: 'Enter', isComposing: true });
    fireEvent.keyDown(field, { key: 'Enter', keyCode: 229 });
    expect(props.onSend).not.toHaveBeenCalled();
    fireEvent.keyDown(field, { key: 'Enter' });
    expect(props.onSend).toHaveBeenCalledWith('にほん');
  });

  it('keeps Send disabled until there is text or a staged file', async () => {
    renderComposer();
    expect(screen.getByRole('button', { name: 'Send message' })).toBeDisabled();
  });

  it('sends a staged file with no text', async () => {
    const { props } = renderComposer({ attachments: [staged] });
    await userEvent.click(screen.getByRole('button', { name: 'Send message' }));
    expect(props.onSend).toHaveBeenCalledWith('');
  });
});

describe('the + menu', () => {
  it('lists the four kinds of file in the design’s order', async () => {
    renderComposer();
    await userEvent.click(screen.getByRole('button', { name: 'Attach a file' }));
    expect(screen.getAllByRole('menuitem').map((item) => item.textContent)).toEqual([
      'Photo or image', '3D model (STL, 3MF, STEP)', 'G-code', 'Document (PDF)',
    ]);
    expect(screen.getByRole('menu').closest('.chat-actions')).toHaveClass('chat-actions--above');
  });

  it.each(ATTACHMENT_CHOICES)('opens the file picker filtered to $label', async ({ label, accept }) => {
    const click = vi.spyOn(HTMLInputElement.prototype, 'click').mockImplementation(() => {});
    const { container } = renderComposer();
    await userEvent.click(screen.getByRole('button', { name: 'Attach a file' }));
    await userEvent.click(screen.getByRole('menuitem', { name: label }));
    const input = container.querySelector('input[type="file"]') as HTMLInputElement;
    expect(click).toHaveBeenCalledTimes(1);
    expect(click.mock.instances[0]).toBe(input);
    expect(input.accept).toBe(accept);
    expect(screen.queryByRole('menu')).toBeNull();
  });

  // In the app the host shows the operating system's dialog: a web view's
  // file input ignores `accept` on macOS and lists every file.
  it.each(ATTACHMENT_CHOICES)('asks the host to pick $label when it can', async ({ label, kind }) => {
    const click = vi.spyOn(HTMLInputElement.prototype, 'click').mockImplementation(() => {});
    const onPickFiles = vi.fn();
    renderComposer({ onPickFiles });
    await userEvent.click(screen.getByRole('button', { name: 'Attach a file' }));
    await userEvent.click(screen.getByRole('menuitem', { name: label }));
    expect(onPickFiles).toHaveBeenCalledWith(kind);
    expect(click).not.toHaveBeenCalled();
  });

  it('asks the host for a picture from the Photo button', async () => {
    const onPickFiles = vi.fn();
    renderComposer({ onPickFiles, photoButton: true });
    await userEvent.click(screen.getByRole('button', { name: 'Add a photo' }));
    expect(onPickFiles).toHaveBeenCalledWith('image');
  });

  it('hands picked files to the host as picker attachments', () => {
    const { container, props } = renderComposer();
    const file = new File(['G1 X0'], 'part.gcode');
    fireEvent.change(container.querySelector('input[type="file"]')!, { target: { files: [file] } });
    expect(props.onAttachFiles).toHaveBeenCalledWith([file], 'picker');
  });

  it('still takes a dropped file and a pasted image', () => {
    const { container, field, props } = renderComposer();
    const photo = new File(['x'], 'bracket.png', { type: 'image/png' });
    fireEvent.drop(container.querySelector('.composer')!, { dataTransfer: { files: [photo] } });
    expect(props.onAttachFiles).toHaveBeenLastCalledWith([photo], 'drop');
    fireEvent.paste(field, { clipboardData: { files: [photo] } });
    expect(props.onAttachFiles).toHaveBeenLastCalledWith([photo], 'clipboard');
  });

  it('shows staged files inside the box, above the text', () => {
    const { container } = renderComposer({ attachments: [staged] });
    const box = container.querySelector('.composer-box')!;
    expect(Array.from(box.children).map((child) => child.className || child.tagName)).toEqual([
      'composer-attachments', 'TEXTAREA', 'composer-toolbar',
    ]);
    expect(screen.getByRole('button', { name: 'Remove bracket-v3.3mf' })).toBeInTheDocument();
  });
});

describe('while the Agent responds', () => {
  it('disables + and offers Stop, but the next message can be typed', async () => {
    const { field, props } = renderComposer({ streaming: true });
    expect(screen.getByRole('button', { name: 'Attach a file' })).toBeDisabled();
    expect(screen.queryByRole('button', { name: 'Send message' })).toBeNull();
    expect(field).toBeEnabled();
    await userEvent.type(field, 'and the lid too{Enter}');
    expect(field.value).toBe('and the lid too');
    expect(props.onSend).not.toHaveBeenCalled();
    await userEvent.click(screen.getByRole('button', { name: 'Stop generating' }));
    expect(props.onStop).toHaveBeenCalled();
  });

  it('closes an open + menu when a reply starts, and does not reopen it after', async () => {
    const { rerender, props } = renderComposer();
    await userEvent.click(screen.getByRole('button', { name: 'Attach a file' }));
    expect(screen.getByRole('menu')).toBeInTheDocument();
    rerender(<Composer {...props} streaming />);
    expect(screen.queryByRole('menu')).toBeNull();
    rerender(<Composer {...props} streaming={false} />);
    expect(screen.queryByRole('menu')).toBeNull();
  });
});

describe('the printer panel’s Photo mode', () => {
  it('keeps the Photo button instead of +', async () => {
    const click = vi.spyOn(HTMLInputElement.prototype, 'click').mockImplementation(() => {});
    renderComposer({ photoButton: true });
    expect(screen.queryByRole('button', { name: 'Attach a file' })).toBeNull();
    await act(async () => { await userEvent.click(screen.getByRole('button', { name: 'Add a photo' })); });
    expect(click).toHaveBeenCalledTimes(1);
  });

  it('has no attachment action at all where nothing can be attached', () => {
    renderComposer({ attachable: false, disabled: true, disabledReason: '' });
    expect(screen.queryByRole('button', { name: 'Attach a file' })).toBeNull();
    expect(screen.queryByRole('button', { name: 'Add a photo' })).toBeNull();
  });
});

// A narrow pane drops "Thinking:" when the full labels do not fit beside +
// and Send. jsdom does no layout, so these tests give the choices a width,
// and a full-length width that only the measuring class reveals: the
// composer must measure with every label at full length.
describe('the pickers in a narrow pane', () => {
  const models = {
    defaultProvider: 'anthropic',
    providers: [{ id: 'anthropic', label: 'Anthropic', defaultModel: 'claude-opus-5-5', state: 'ready' as const,
      models: [{ id: 'claude-opus-5-5', label: 'Claude Opus 5.5', efforts: ['low', 'medium', 'high'] }] }],
  };
  const chatModel = { provider: 'anthropic', model: 'claude-opus-5-5', effort: 'medium' };
  function layOut(boxWidth: number, fullWidth: number) {
    const measured = (element: HTMLElement) => element.classList.contains('composer-choices');
    vi.spyOn(HTMLElement.prototype, 'clientWidth', 'get').mockImplementation(function (this: HTMLElement) {
      return measured(this) ? boxWidth : 0;
    });
    vi.spyOn(HTMLElement.prototype, 'scrollWidth', 'get').mockImplementation(function (this: HTMLElement) {
      if (!measured(this)) return 0;
      return this.classList.contains('composer-choices--measuring') ? fullWidth : Math.min(fullWidth, boxWidth);
    });
  }
  const choices = () => document.querySelector('.composer-choices')!;

  it('keeps the full labels where they fit', () => {
    layOut(381, 330);
    renderComposer({ models, chatModel, onChooseModel: vi.fn() });
    expect(choices()).not.toHaveClass('composer-choices--narrow');
    expect(screen.getByRole('button', { name: 'Thinking: Medium' })).toHaveTextContent('Thinking: Medium');
  });

  it('drops "Thinking:" where they do not, and keeps the full name for assistive tech and the tooltip', () => {
    layOut(220, 330);
    renderComposer({ models, chatModel, onChooseModel: vi.fn() });
    expect(choices()).toHaveClass('composer-choices--narrow');
    expect(choices()).not.toHaveClass('composer-choices--measuring');
    const thinking = screen.getByRole('button', { name: 'Thinking: Medium' });
    expect(thinking).toHaveAttribute('title', 'Thinking: Medium');
    expect(screen.getByRole('button', { name: 'Model: Claude Opus 5.5' })).toHaveAttribute('title', 'Model: Claude Opus 5.5');
  });
});
