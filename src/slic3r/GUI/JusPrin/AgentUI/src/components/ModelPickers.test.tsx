// The composer's model and thinking pickers: what each menu offers, and how a
// choice changes when the model does.

import { cleanup, render, screen } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { describe, expect, it, vi } from 'vitest';
import { ChatModelChoice, ModelCatalogPayload } from '../bridge/protocol';
import { ModelPickers, modelMenu, switchModel } from './ModelPickers';

const opus = { id: 'claude-opus-5-5', label: 'Claude Opus 5.5', efforts: ['low', 'medium', 'high', 'xhigh', 'max'] };
const sonnet = { id: 'claude-sonnet-5-5', label: 'Claude Sonnet 5.5', efforts: ['low', 'medium', 'high'] };
const mini = { id: 'gpt-5.4-mini', label: 'GPT-5.4 mini', efforts: ['low', 'medium', 'high'] };
const deepseek = { id: 'deepseek/deepseek-v3', label: 'DeepSeek V3', efforts: [] };

const two: ModelCatalogPayload = {
  defaultProvider: 'anthropic',
  providers: [
    { id: 'anthropic', label: 'Anthropic', defaultModel: 'claude-opus-5-5', state: 'ready', models: [opus, sonnet] },
    { id: 'openai', label: 'OpenAI', defaultModel: 'gpt-5.4-mini', state: 'ready', models: [mini] },
  ],
};

const onOpus: ChatModelChoice = { provider: 'anthropic', model: 'claude-opus-5-5', effort: 'medium' };

function renderPickers(catalog: ModelCatalogPayload, choice: ChatModelChoice, onChoose = vi.fn()) {
  render(<ModelPickers catalog={catalog} choice={choice} disabled={false} onChoose={onChoose} />);
  return { onChoose };
}

describe('the model menu', () => {
  it('groups models under the provider name when more than one is set up, and offers nothing else', () => {
    const entries = modelMenu(two, onOpus, vi.fn());
    expect(entries.map((entry) => ('section' in entry ? `# ${entry.section}` : entry.label))).toEqual([
      '# Anthropic', 'Claude Opus 5.5', 'Claude Sonnet 5.5', '# OpenAI', 'GPT-5.4 mini',
    ]);
  });

  it('leaves the heading out with a single provider', () => {
    const one = { ...two, providers: [two.providers[1]] };
    const entries = modelMenu(one, { provider: 'openai', model: 'gpt-5.4-mini', effort: '' }, vi.fn());
    expect(entries.some((entry) => 'section' in entry)).toBe(false);
  });

  it('keeps the chat\'s model choosable before its list arrives, and says the list is loading', () => {
    const loading: ModelCatalogPayload = {
      defaultProvider: 'openai',
      providers: [{ id: 'openai', label: 'OpenAI', defaultModel: 'gpt-5.4-mini', state: 'loading', models: [] }],
    };
    const entries = modelMenu(loading, { provider: 'openai', model: 'gpt-5.4-mini', effort: '' }, vi.fn());
    expect(entries.map((entry) => ('section' in entry ? '' : `${entry.label}${entry.disabled ? ' (disabled)' : ''}`)))
      .toEqual(['gpt-5.4-mini', 'Loading models… (disabled)']);
  });

  it('switches model and provider together, from the next turn', async () => {
    const { onChoose } = renderPickers(two, onOpus);
    await userEvent.click(screen.getByRole('button', { name: 'Model: Claude Opus 5.5' }));
    expect(screen.getByRole('menuitemradio', { name: 'Claude Opus 5.5' })).toHaveAttribute('aria-checked', 'true');
    await userEvent.click(screen.getByRole('menuitemradio', { name: 'GPT-5.4 mini' }));
    expect(onChoose).toHaveBeenCalledWith({ provider: 'openai', model: 'gpt-5.4-mini', effort: 'medium' });
  });

});

describe('the thinking level', () => {
  it('keeps the level when the new model has it, and otherwise leaves it to the model', () => {
    expect(switchModel({ ...onOpus, effort: 'high' }, 'anthropic', sonnet).effort).toBe('high');
    expect(switchModel({ ...onOpus, effort: 'max' }, 'anthropic', sonnet).effort).toBe('');
  });

  it('lists only the chosen model\'s levels, and marks the current one', async () => {
    const { onChoose } = renderPickers(two, { provider: 'anthropic', model: 'claude-sonnet-5-5', effort: 'medium' });
    await userEvent.click(screen.getByRole('button', { name: 'Thinking: Medium' }));
    expect(screen.getAllByRole('menuitemradio').map((item) => item.textContent)).toEqual(['Low', 'Medium', 'High']);
    await userEvent.click(screen.getByRole('menuitemradio', { name: 'High' }));
    expect(onChoose).toHaveBeenCalledWith({ provider: 'anthropic', model: 'claude-sonnet-5-5', effort: 'high' });
  });

  it('reads "Default" until a level is chosen, unless the provider names the default', () => {
    renderPickers(two, { ...onOpus, effort: '' });
    expect(screen.getByRole('button', { name: /^Thinking:/ })).toHaveTextContent('Thinking: Default');
  });

  it('is left out for a model with no levels', () => {
    const catalog: ModelCatalogPayload = {
      defaultProvider: 'openrouter',
      providers: [{ id: 'openrouter', label: 'OpenRouter', defaultModel: 'openai/gpt-5.4-mini', state: 'ready', models: [deepseek] }],
    };
    renderPickers(catalog, { provider: 'openrouter', model: 'deepseek/deepseek-v3', effort: '' });
    expect(screen.getByRole('button', { name: 'Model: DeepSeek V3' })).toBeInTheDocument();
    expect(screen.queryByRole('button', { name: /^Thinking:/ })).toBeNull();
  });
});

// A long model list (OpenRouter's runs to hundreds) opens with a search field.
describe('the model menu search', () => {
  const routed = (n: number) => Array.from({ length: n }, (_, i) =>
    ({ id: `vendor/model-${i + 1}`, label: `Vendor: Model ${i + 1}`, efforts: [] }));
  const withRouter = (n: number): ModelCatalogPayload => ({
    ...two,
    providers: [...two.providers,
      { id: 'openrouter', label: 'OpenRouter', defaultModel: 'vendor/model-1', state: 'ready', models: routed(n) }],
  });
  const openMenu = () => userEvent.click(screen.getByRole('button', { name: 'Model: Claude Opus 5.5' }));
  const rows = () => screen.queryAllByRole('menuitemradio').map((row) => row.textContent);
  const headings = () => Array.from(document.querySelectorAll('.chat-menu-section')).map((h) => h.textContent);

  it('appears only when the menu lists more than 12 models', async () => {
    // Anthropic 2 + OpenAI 1 + OpenRouter 9 = 12.
    renderPickers(withRouter(9), onOpus);
    await openMenu();
    expect(screen.queryByRole('searchbox')).toBeNull();
    expect(screen.getByRole('menuitemradio', { name: 'Claude Opus 5.5' })).toHaveFocus();
  });

  it('opens with the focus in the field, and counts a long provider\'s models', async () => {
    renderPickers(withRouter(10), onOpus);
    await openMenu();
    expect(screen.getByRole('searchbox', { name: 'Search models' })).toHaveFocus();
    expect(headings()).toEqual(['Anthropic', 'OpenAI', 'OpenRouter']);
    cleanup();
    renderPickers(withRouter(13), onOpus);
    await openMenu();
    expect(headings()).toEqual(['Anthropic', 'OpenAI', 'OpenRouter · 13 models']);
  });

  it('filters on the model or the provider name, ignoring case, and drops providers with no match', async () => {
    renderPickers(withRouter(13), onOpus);
    await openMenu();
    await userEvent.type(screen.getByRole('searchbox'), 'SONNET');
    expect(rows()).toEqual(['Claude Sonnet 5.5']);
    expect(headings()).toEqual(['Anthropic']);
    await userEvent.clear(screen.getByRole('searchbox'));
    await userEvent.type(screen.getByRole('searchbox'), 'openai');
    expect(rows()).toEqual(['GPT-5.4 mini']);
    // The count is the whole provider's, so it is left out while filtering.
    await userEvent.clear(screen.getByRole('searchbox'));
    await userEvent.type(screen.getByRole('searchbox'), 'model 1');
    expect(headings()).toEqual(['OpenRouter']);
    expect(rows()).toEqual(['Vendor: Model 1', 'Vendor: Model 10', 'Vendor: Model 11', 'Vendor: Model 12', 'Vendor: Model 13']);
  });

  it('says so when nothing matches', async () => {
    renderPickers(withRouter(13), onOpus);
    await openMenu();
    await userEvent.type(screen.getByRole('searchbox'), 'llama 9');
    expect(screen.queryByRole('menu')).toBeNull();
    expect(screen.getByRole('status')).toHaveTextContent('No models match “llama 9”.');
  });

  it('highlights the first match, moves with Up and Down, and picks with Enter', async () => {
    const { onChoose } = renderPickers(withRouter(13), onOpus);
    await openMenu();
    const field = screen.getByRole('searchbox');
    const highlighted = () => document.getElementById(field.getAttribute('aria-activedescendant')!)?.textContent;
    // Before typing, the chat's own model.
    expect(highlighted()).toBe('Claude Opus 5.5');
    await userEvent.type(field, 'claude');
    expect(highlighted()).toBe('Claude Opus 5.5');
    await userEvent.keyboard('{ArrowDown}');
    expect(highlighted()).toBe('Claude Sonnet 5.5');
    await userEvent.keyboard('{ArrowDown}');
    expect(highlighted()).toBe('Claude Opus 5.5');
    await userEvent.keyboard('{ArrowUp}');
    expect(highlighted()).toBe('Claude Sonnet 5.5');
    expect(field).toHaveFocus();
    await userEvent.keyboard('{Enter}');
    expect(onChoose).toHaveBeenCalledWith({ provider: 'anthropic', model: 'claude-sonnet-5-5', effort: 'medium' });
    expect(screen.queryByRole('searchbox')).toBeNull();
    expect(screen.getByRole('button', { name: 'Model: Claude Opus 5.5' })).toHaveFocus();
  });

  it('clears the search on the first Escape and closes on the second', async () => {
    renderPickers(withRouter(13), onOpus);
    await openMenu();
    await userEvent.type(screen.getByRole('searchbox'), 'sonnet');
    await userEvent.keyboard('{Escape}');
    expect(screen.getByRole('searchbox')).toHaveValue('');
    expect(rows()).toHaveLength(16);
    await userEvent.keyboard('{Escape}');
    expect(screen.queryByRole('searchbox')).toBeNull();
    expect(screen.getByRole('button', { name: 'Model: Claude Opus 5.5' })).toHaveFocus();
  });
});
