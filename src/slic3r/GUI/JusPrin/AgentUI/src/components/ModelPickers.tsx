// The composer's model and thinking pickers (the Composer proposal on the
// "JusPrin v2" canvas, states 5 to 8). The model menu lists every model of
// every provider with a key, under the provider's name when there is more
// than one; the thinking menu lists the levels the chosen model accepts and
// is left out when it accepts none. A choice belongs to the active chat and
// applies from its next turn. A model menu of more than SEARCH_FROM models
// (OpenRouter's runs to hundreds) starts with a search field, and a provider
// with that many shows its count beside its name.

import { ChatModelChoice, ModelCatalogPayload, ModelOptionInfo } from '../bridge/protocol';
import { ActionMenu, MenuItem, MenuSearch, MenuSection } from './ChatNavigation';

const EFFORT_LABELS: Record<string, string> = {
  none: 'Off', minimal: 'Minimal', low: 'Low', medium: 'Medium', high: 'High', xhigh: 'Extra high', max: 'Max',
  // JusPrin's own levels, which its gateway maps to the model's.
  fast: 'Fast', thorough: 'Thorough',
};

export const SEARCH_FROM = 12;

const MODEL_SEARCH: MenuSearch = { label: 'Search models', noMatch: (query) => `No models match “${query}”.` };

export function effortLabel(effort: string): string {
  return EFFORT_LABELS[effort] ?? effort;
}

// The catalog's entry for the chosen model, if its list has arrived.
export function findModel(catalog: ModelCatalogPayload, choice: ChatModelChoice): ModelOptionInfo | undefined {
  return catalog.providers.find((provider) => provider.id === choice.provider)?.models
    .find((model) => model.id === choice.model);
}

// Switching model keeps the thinking level when the new model has it, and
// otherwise leaves the level to the new model's default.
export function switchModel(choice: ChatModelChoice, provider: string, model: ModelOptionInfo): ChatModelChoice {
  return { provider, model: model.id, effort: model.efforts.includes(choice.effort) ? choice.effort : '' };
}

export function modelMenu(catalog: ModelCatalogPayload, choice: ChatModelChoice,
  onChoose: (choice: ChatModelChoice) => void): (MenuItem | MenuSection)[] {
  const grouped = catalog.providers.length > 1;
  const entries: (MenuItem | MenuSection)[] = [];
  for (const provider of catalog.providers) {
    if (grouped)
      entries.push({ section: provider.label,
        detail: provider.models.length > SEARCH_FROM ? `${provider.models.length} models` : undefined });
    const models = [...provider.models];
    // The chat's model stays choosable even before its list arrives, or when
    // the list leaves it out.
    if (provider.id === choice.provider && !models.some((model) => model.id === choice.model))
      models.unshift({ id: choice.model, label: choice.model, efforts: [] });
    for (const model of models)
      entries.push({
        key: `${provider.id}/${model.id}`,
        label: model.label,
        current: provider.id === choice.provider && model.id === choice.model,
        matches: [model.label, provider.label],
        onSelect: () => onChoose(switchModel(choice, provider.id, model)),
      });
    if (provider.state === 'loading')
      entries.push({ key: `${provider.id}/loading`, label: 'Loading models…', disabled: true,
        matches: [provider.label], onSelect: () => {} });
    if (provider.state === 'failed')
      entries.push({ key: `${provider.id}/failed`, label: 'Couldn’t load the model list', disabled: true,
        matches: [provider.label], onSelect: () => {} });
  }
  return entries;
}

export function ModelPickers({ catalog, choice, disabled, onChoose }: {
  catalog: ModelCatalogPayload;
  choice: ChatModelChoice;
  disabled: boolean;
  onChoose: (choice: ChatModelChoice) => void;
}) {
  const model = findModel(catalog, choice);
  const efforts = model?.efforts ?? [];
  // An unset level shows the model's own default where the provider names
  // it; where it does not, it reads "Default".
  const shownEffort = choice.effort || model?.defaultEffort || '';
  const models = modelMenu(catalog, choice, onChoose);
  const modelCount = models.filter((entry) => !('section' in entry) && !entry.disabled).length;
  // Only the model picker gives up width in a narrow pane; the thinking
  // picker drops its "Thinking:" instead (Composer measures which).
  return <>
    <ActionMenu label="Model" className="composer-model-menu" text={model?.label ?? choice.model} above
      disabled={disabled} items={models} search={modelCount > SEARCH_FROM ? MODEL_SEARCH : undefined} />
    {efforts.length > 0 && <ActionMenu label="Thinking" above disabled={disabled} prefixed
      text={shownEffort ? effortLabel(shownEffort) : 'Default'}
      items={efforts.map((effort) => ({
        key: effort,
        label: effortLabel(effort),
        current: effort === shownEffort,
        onSelect: () => onChoose({ ...choice, effort }),
      }))} />}
  </>;
}
