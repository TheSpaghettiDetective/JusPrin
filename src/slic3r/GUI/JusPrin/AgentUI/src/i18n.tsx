import { i18n, type Messages } from '@lingui/core';
import { TransNoContext, type TransProps } from '@lingui/react/server';
import languages from '../../Localization/languages.json';

// Every page language's catalog, compiled from ../../Localization/pages by
// @lingui/vite-plugin. The bundle is one self-contained file, so they are all
// inlined; Home draws through this same module.
const catalogs = import.meta.glob<{ messages: Messages }>('../../Localization/pages/*.po', { eager: true });

export type PageLanguage = { code: string; tag: string };

// The app passes its interface language as ?lang=<code>, the code its own
// catalogs use (de, pt_BR, zh_CN). A code with a region nobody translated for
// (de_AT) finds its language; anything else reads English.
export function pageLanguage(requested: string | null | undefined): PageLanguage {
  const list = languages.languages;
  const english = list.find((l) => l.code === languages.source)!;
  if (!requested) return english;
  const code = requested.replace('-', '_');
  const exact = list.find((l) => l.code.toLowerCase() === code.toLowerCase());
  if (exact) return exact;
  const base = code.split('_')[0].toLowerCase();
  if (base === 'zh') {
    const traditional = /_(TW|HK|MO)$|Hant/i.test(code);
    return list.find((l) => l.code === (traditional ? 'zh_TW' : 'zh_CN'))!;
  }
  return list.find((l) => l.code.split('_')[0] === base) ?? english;
}

export function activateLanguage(requested: string | null | undefined): PageLanguage {
  const language = pageLanguage(requested);
  const catalog = catalogs[`../../Localization/pages/${language.code}.po`];
  i18n.load(language.code, catalog?.messages ?? {});
  // Plural rules come from Intl, which wants the BCP 47 tag.
  i18n.activate(language.code, [language.tag]);
  // The fonts pick CJK glyphs from the document language.
  if (typeof document !== 'undefined') document.documentElement.lang = language.tag;
  return language;
}

// The locale a page writes a date, a time or a number in. English keeps the
// formats the screens were designed with (`english`: en-US for "Oct 24",
// en-GB for "5 November" or a 24-hour clock, undefined for the computer's
// own); another language writes them its own way.
export function dateLocale(english: string): string;
export function dateLocale(english: string | undefined): string | undefined;
export function dateLocale(english: string | undefined): string | undefined {
  if (i18n.locale === languages.source || typeof document === 'undefined') return english;
  return document.documentElement.lang || english;
}

// A number as the page's language writes it: "0.4" in English, "0,4" in
// German, with at least `min` and at most `max` decimals. Only for words the
// person reads; the model's own words keep a decimal point.
export function decimalText(value: number, min: number, max: number): string {
  return i18n.number(value, { minimumFractionDigits: min, maximumFractionDigits: max });
}

// The app passes its language in the page URL. Activating on import, before
// any other module runs, lets text translated at a module's top level read
// the right language too: each page's main.tsx imports this module first.
activateLanguage(typeof window === 'undefined' ? null : new URLSearchParams(window.location.search).get('lang'));

export { i18n };

// The macros from @lingui/react/macro compile to these two instead of
// @lingui/react's (runtimeConfigModule in lingui.config.ts). The language is
// chosen once, before the first render, and never changes while the page
// runs, so nothing needs I18nProvider's context, and a component renders the
// same in a test as on the page.
export function Trans(props: TransProps) {
  return <TransNoContext {...props} lingui={{ i18n }} />;
}

export function useLingui() {
  return { i18n, _: i18n._.bind(i18n) };
}
