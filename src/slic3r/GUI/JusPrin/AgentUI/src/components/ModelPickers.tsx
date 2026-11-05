// The composer's model and thinking pickers (the Composer proposal on the
// "JusPrin v2" canvas, states 5 to 8). The model menu lists every model of
// every provider with a key, under the provider's name when there is more
// than one; the thinking menu lists the levels the chosen model accepts and
// is left out when it accepts none. A choice belongs to the active chat and
// applies from its next turn. A model menu of more than SEARCH_FROM models
// (OpenRouter's runs to hundreds) starts with a search field, and a provider
// with that many shows its count beside its name.
//
// An account's models carry their price and what they are for; models on the
// person's own key name no price. A chat keeps the model and thinking level it
// started with, so once the person has sent a message both pickers are greyed,
// and hovering either says why and what a message costs.

import { ChatModelChoice, CreditAccount, ModelCatalogPayload, ModelOptionInfo } from '../bridge/protocol';
import { messageCostSentence, priceWords } from '../credits';
import { ActionMenu, MenuItem, MenuSearch, MenuSection } from './ChatNavigation';

const EFFORT_LABELS: Record<string, string> = {
  none: 'Off', minimal: 'Minimal', low: 'Low', medium: 'Medium', high: 'High', xhigh: 'Extra high', max: 'Max',
  // JusPrin's own levels, which its gateway maps to the model's.
  fast: 'Fast', thorough: 'Thorough',
};

export const SEARCH_FROM = 12;

const TIER_USES: Record<string, string> = {
  basic: 'setting up an ordinary print',
  expert: 'troubleshooting, tricky models, anything you’d ask a person',
};

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
  onChoose: (choice: ChatModelChoice) => void, account?: CreditAccount | null): (MenuItem | MenuSection)[] {
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
    for (const model of models) {
      const price = priceWords(model, account);
      entries.push({
        key: `${provider.id}/${model.id}`,
        label: price ? `${model.label} · ${price}` : model.label,
        detail: model.tier ? TIER_USES[model.tier] : undefined,
        current: provider.id === choice.provider && model.id === choice.model,
        matches: [model.label, provider.label],
        onSelect: () => onChoose(switchModel(choice, provider.id, model)),
      });
    }
    if (provider.state === 'loading')
      entries.push({ key: `${provider.id}/loading`, label: 'Loading models…', disabled: true,
        matches: [provider.label], onSelect: () => {} });
    if (provider.state === 'failed')
      entries.push({ key: `${provider.id}/failed`, label: 'Couldn’t load the model list', disabled: true,
        matches: [provider.label], onSelect: () => {} });
  }
  return entries;
}

export function ModelPickers({ catalog, choice, disabled, locked = false, account, onChoose }: {
  catalog: ModelCatalogPayload;
  choice: ChatModelChoice;
  disabled: boolean;
  // The person has sent a message in this chat.
  locked?: boolean;
  account?: CreditAccount | null;
  onChoose: (choice: ChatModelChoice) => void;
}) {
  const model = findModel(catalog, choice);
  const efforts = model?.efforts ?? [];
  // An unset level shows the model's own default where the provider names
  // it; where it does not, it reads "Default".
  const shownEffort = choice.effort || model?.defaultEffort || '';
  const models = modelMenu(catalog, choice, onChoose, account);
  const cost = messageCostSentence(model, account);
  const priced = catalog.providers.some((provider) => provider.models.some((option) => option.credits !== undefined));
  const modelCount = models.filter((entry) => !('section' in entry) && !entry.disabled).length;
  // Only the model picker gives up width in a narrow pane; the thinking
  // picker drops its "Thinking:" instead (Composer measures which).
  return <div className={locked ? 'composer-pickers composer-pickers--locked' : 'composer-pickers'}>
    <ActionMenu label="Model" className={priced ? 'composer-model-menu composer-model-menu--priced' : 'composer-model-menu'}
      text={model?.label ?? choice.model} above
      disabled={disabled || locked} items={models} search={modelCount > SEARCH_FROM ? MODEL_SEARCH : undefined} />
    {efforts.length > 0 && <ActionMenu label="Thinking" above disabled={disabled || locked} prefixed
      text={shownEffort ? effortLabel(shownEffort) : 'Default'}
      items={efforts.map((effort) => ({
        key: effort,
        label: effortLabel(effort),
        current: effort === shownEffort,
        onSelect: () => onChoose({ ...choice, effort }),
      }))} />}
    {locked && <div className="composer-pickers-tooltip" role="tooltip">
      <p>A chat keeps the model and thinking level it started with. Start a new chat to change them.</p>
      {cost && <p>{cost}</p>}
    </div>}
  </div>;
}
