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
import { plural, t } from '@lingui/core/macro';
import { Trans } from '@lingui/react/macro';

const EFFORT_LABELS: Record<string, string> = {
  // TRN: how hard the AI model thinks before it answers.
  none: t({ message: 'Off', comment: 'A thinking level of the AI model: no thinking' }),
  minimal: t({ message: 'Minimal', comment: 'A thinking level of the AI model' }),
  low: t({ message: 'Low', comment: 'A thinking level of the AI model' }),
  medium: t({ message: 'Medium', comment: 'A thinking level of the AI model' }),
  high: t({ message: 'High', comment: 'A thinking level of the AI model' }),
  xhigh: t({ message: 'Extra high', comment: 'A thinking level of the AI model' }),
  max: t({ message: 'Max', comment: 'A thinking level of the AI model: the most' }),
  // JusPrin's own levels, which its gateway maps to the model's.
  fast: t({ message: 'Fast', comment: 'A thinking level of the AI model' }),
  thorough: t({ message: 'Thorough', comment: 'A thinking level of the AI model' }),
};

export const SEARCH_FROM = 12;

const TIER_USES: Record<string, string> = {
  // TRN: what a Standard model is for, under its name in the model menu.
  basic: t`setting up an ordinary print`,
  // TRN: what an Expert model is for, under its name in the model menu.
  expert: t`troubleshooting, tricky models, anything you’d ask a person`,
};

const MODEL_SEARCH: MenuSearch = { label: t`Search models`, noMatch: (query) => t`No models match “${query}”.` };

export function effortLabel(effort: string): string {
  return EFFORT_LABELS[effort] ?? effort;
}

// The catalog's entry for the chosen model, if its list has arrived.
export function findModel(catalog: ModelCatalogPayload, choice: ChatModelChoice): ModelOptionInfo | undefined {
  return catalog.providers.find((provider) => provider.id === choice.provider)?.models
    .find((model) => model.id === choice.model);
}

// What the chosen model is called when its list has none of it. JusPrin's
// model ids are aliases ("slicer-1") that mean nothing to anyone, so until
// its list names the model the chat's model reads as JusPrin. Every other
// provider's id is the model's own name. A chat whose account was signed out
// of has no list at all, and the host sends the name with its choice.
function unlistedLabel(catalog: ModelCatalogPayload, choice: ChatModelChoice): string {
  if (choice.provider !== 'jusprin') return choice.model;
  return catalog.providers.find((option) => option.id === choice.provider)?.label ?? choice.providerLabel ?? choice.model;
}

// What follows a provider's name above its models: what JusPrin's own AI is
// for, or how many models a long list holds.
function providerDetail(provider: ModelCatalogPayload['providers'][number]): string | undefined {
  if (provider.id === 'jusprin') return t`Specializes in 3D printing`;
  const count = provider.models.length;
  return count > SEARCH_FROM ? plural(count, { one: '# model', other: '# models' }) : undefined;
}

// Under a model's name: what an account's model is for, or, for the model on
// this computer, the app that runs it and that it costs nothing.
function modelDetail(provider: ModelCatalogPayload['providers'][number], model: ModelOptionInfo): string | undefined {
  if (model.tier) return TIER_USES[model.tier];
  if (provider.id !== 'local') return undefined;
  const runtime = provider.runtime;
  return runtime ? t`${runtime} · no credits` : t`No credits`;
}

// Switching model keeps the thinking level when the new model has it, and
// otherwise leaves the level to the new model's default.
export function switchModel(choice: ChatModelChoice, provider: string, model: ModelOptionInfo): ChatModelChoice {
  return { provider, model: model.id, effort: model.efforts.includes(choice.effort) ? choice.effort : '' };
}

export function modelMenu(catalog: ModelCatalogPayload, choice: ChatModelChoice,
  onChoose: (choice: ChatModelChoice) => void, account?: CreditAccount | null,
  onGetNewVersion?: () => void): (MenuItem | MenuSection)[] {
  const grouped = catalog.providers.length > 1;
  const entries: (MenuItem | MenuSection)[] = [];
  for (const provider of catalog.providers) {
    if (grouped)
      entries.push({ section: provider.label, detail: providerDetail(provider) });
    const models = [...provider.models];
    // The chat's model stays choosable even before its list arrives, or when
    // the list leaves it out. A list the server refused for this release has
    // nothing to choose: Get the new version… takes its place.
    const outdated = provider.state === 'failed' && provider.updateMessage !== undefined && onGetNewVersion !== undefined;
    if (provider.id === choice.provider && !outdated && !models.some((model) => model.id === choice.model))
      models.unshift({ id: choice.model, label: unlistedLabel(catalog, choice), efforts: [] });
    for (const model of models) {
      const price = priceWords(model, account);
      entries.push({
        key: `${provider.id}/${model.id}`,
        label: price ? `${model.label} · ${price}` : model.label,
        detail: modelDetail(provider, model),
        current: provider.id === choice.provider && model.id === choice.model,
        matches: [model.label, provider.label],
        onSelect: () => onChoose(switchModel(choice, provider.id, model)),
      });
    }
    if (provider.state === 'loading')
      entries.push({ key: `${provider.id}/loading`, label: t`Loading models…`, disabled: true,
        matches: [provider.label], onSelect: () => {} });
    if (outdated)
      entries.push({ key: `${provider.id}/update`, label: t`Get the new version…`,
        detail: t`This version of JusPrin can no longer connect to JusPrin accounts.`,
        matches: [provider.label], onSelect: onGetNewVersion });
    else if (provider.state === 'failed')
      entries.push({ key: `${provider.id}/failed`, label: provider.updateMessage ?? t`Couldn’t load the model list`, disabled: true,
        matches: [provider.label], onSelect: () => {} });
  }
  return entries;
}

export function ModelPickers({ catalog, choice, disabled, locked = false, account, onChoose, onGetNewVersion }: {
  catalog: ModelCatalogPayload;
  choice: ChatModelChoice;
  disabled: boolean;
  // The person has sent a message in this chat.
  locked?: boolean;
  account?: CreditAccount | null;
  onChoose: (choice: ChatModelChoice) => void;
  onGetNewVersion?: () => void;
}) {
  const model = findModel(catalog, choice);
  const efforts = model?.efforts ?? [];
  // An unset level shows the model's own default where the provider names
  // it; where it does not, it reads "Default".
  const shownEffort = choice.effort || model?.defaultEffort || '';
  const models = modelMenu(catalog, choice, onChoose, account, onGetNewVersion);
  const cost = messageCostSentence(model, account);
  const priced = catalog.providers.some((provider) => provider.models.some((option) => option.credits !== undefined));
  const modelCount = models.filter((entry) => !('section' in entry) && !entry.disabled).length;
  // Only the model picker gives up width in a narrow pane; the thinking
  // picker drops its "Thinking:" instead (Composer measures which).
  return <div className={locked ? 'composer-pickers composer-pickers--locked' : 'composer-pickers'}>
    <ActionMenu label={t`Model`} className={priced ? 'composer-model-menu composer-model-menu--priced' : 'composer-model-menu'}
      text={model?.label ?? unlistedLabel(catalog, choice)} above
      disabled={disabled || locked} items={models} search={modelCount > SEARCH_FROM ? MODEL_SEARCH : undefined} />
    {efforts.length > 0 && <ActionMenu label={t`Thinking`} above disabled={disabled || locked} prefixed
      text={shownEffort ? effortLabel(shownEffort) : t({ message: 'Default', comment: 'A thinking level: the model decides' })}
      items={efforts.map((effort) => ({
        key: effort,
        label: effortLabel(effort),
        current: effort === shownEffort,
        onSelect: () => onChoose({ ...choice, effort }),
      }))} />}
    {locked && <div className="composer-pickers-tooltip" role="tooltip">
      <p><Trans>A chat keeps the model and thinking level it started with. Start a new chat to change them.</Trans></p>
      {cost && <p>{cost}</p>}
    </div>}
  </div>;
}
