import { readFileSync, readdirSync } from 'node:fs';
import { resolve } from 'node:path';
import { compileMessageOrThrow, type CompiledMessage } from '@lingui/message-utils/compileMessage';
import languages from '../../Localization/languages.json';

// JusPrin's own translation catalogs (Localization/README.md): every
// language translates every entry, keeps its placeholders and line breaks,
// and gives each plural form its own entry. Run update_catalogs.py after
// marking text; this fails until the new entries are translated.

type Entry = { context?: string; id: string; plural?: string; strings: string[]; fuzzy: boolean };

const localization = resolve(__dirname, '../../Localization') + '/';

function unquote(line: string): string {
  return JSON.parse(line.replace(/^[a-z_[\]0-9]+\s+/, ''));
}

function parse(text: string): { header: string; entries: Entry[] } {
  const entries: Entry[] = [];
  let header = '';
  for (const block of text.split(/\n\s*\n/)) {
    const lines = block.split('\n').filter((l) => l && !l.startsWith('#~'));
    if (!lines.some((l) => l.startsWith('msgid'))) continue;
    const fuzzy = lines.some((l) => l.startsWith('#,') && l.includes('fuzzy'));
    const fields: Record<string, string> = {};
    let field = '';
    for (const line of lines) {
      if (line.startsWith('#')) continue;
      const m = /^(msgctxt|msgid_plural|msgid|msgstr(?:\[\d+\])?)\s/.exec(line);
      if (m) { field = m[1]; fields[field] = unquote(line); } else if (line.startsWith('"')) fields[field] += JSON.parse(line);
    }
    if (fields.msgid === '') { header = fields.msgstr ?? ''; continue; }
    const strings = Object.keys(fields).filter((k) => k.startsWith('msgstr')).sort().map((k) => fields[k]);
    entries.push({ context: fields.msgctxt, id: fields.msgid, plural: fields.msgid_plural, strings, fuzzy });
  }
  return { header, entries };
}

function count(text: string, re: RegExp): Map<string, number> {
  const found = new Map<string, number>();
  for (const m of text.matchAll(re)) found.set(m[0], (found.get(m[0]) ?? 0) + 1);
  return found;
}

const same = (a: Map<string, number>, b: Map<string, number>) =>
  a.size === b.size && [...a].every(([k, v]) => b.get(k) === v);

// Native: printf and boost placeholders, and line breaks.
const NATIVE_MARKS = /%(?:\d+%|[-+ #0]*\d*(?:\.\d+)?(?:l|ll|z|h)?[sdufgxXc%])|\n/g;
// Pages: numbered tags (<0>, </0>, <1/>); the arguments come from compiling.
const PAGE_TAGS = /<\/?\d+\/?>/g;

// The argument names a compiled message uses, at any depth: a translation
// must use the same ones. An argument lost to ICU quoting (an apostrophe
// before a brace) shows here as missing.
function argumentNames(message: CompiledMessage): Set<string> {
  const names = new Set<string>();
  const walk = (tokens: CompiledMessage) => {
    for (const token of tokens) {
      if (typeof token === 'string') continue;
      const [name, , format] = token;
      names.add(name);
      if (format && typeof format === 'object')
        for (const [key, branch] of Object.entries(format as Record<string, CompiledMessage>))
          if (key !== 'offset' && Array.isArray(branch)) walk(branch);
    }
  };
  walk(message);
  return names;
}
const sameSet = (a: Set<string>, b: Set<string>) => a.size === b.size && [...a].every((x) => b.has(x));

const translated = languages.languages.filter((l) => l.code !== languages.source);

// The plural categories a translation's plural and selectordinal arguments
// leave out that its language needs (Russian needs one, few, many, other).
function missingPluralForms(message: CompiledMessage, tag: string): string[] {
  const needed = new Intl.PluralRules(tag).resolvedOptions().pluralCategories as string[];
  const missing: string[] = [];
  const walk = (tokens: CompiledMessage) => {
    for (const token of tokens) {
      if (typeof token === 'string') continue;
      const [, type, format] = token;
      if (format && typeof format === 'object') {
        const choices = format as Record<string, CompiledMessage>;
        if (type === 'plural') missing.push(...needed.filter((category) => !(category in choices)));
        for (const [key, branch] of Object.entries(choices)) if (key !== 'offset' && Array.isArray(branch)) walk(branch);
      }
    }
  };
  walk(message);
  return [...new Set(missing)];
}

describe.each(translated.map((l) => [l.code, l.tag]))('%s catalogs', (code, tag) => {
  it('translates every native entry and keeps its placeholders', () => {
    const template = parse(readFileSync(`${localization}native/JusPrin.pot`, 'utf8')).entries;
    const { header, entries } = parse(readFileSync(`${localization}native/${code}.po`, 'utf8'));
    const forms = Number(/nplurals=(\d+)/.exec(header)?.[1]);
    expect(forms, 'Plural-Forms header').toBeGreaterThan(0);
    const byKey = new Map(entries.map((e) => [`${e.context ?? ''}\u0004${e.id}`, e]));
    const problems: string[] = [];
    for (const want of template) {
      const e = byKey.get(`${want.context ?? ''}\u0004${want.id}`);
      if (!e) { problems.push(`missing: ${want.id}`); continue; }
      if (e.fuzzy) problems.push(`fuzzy: ${e.id}`);
      if (e.plural !== undefined && e.strings.length !== forms) problems.push(`needs ${forms} plural forms: ${e.id}`);
      e.strings.forEach((s, i) => {
        const source = i > 0 && e.plural !== undefined ? e.plural : e.id;
        if (!s) problems.push(`untranslated: ${e.id}`);
        // A language with one plural form writes it from the plural source.
        else if (!same(count(s, NATIVE_MARKS), count(source, NATIVE_MARKS)) &&
                 !(e.plural !== undefined && same(count(s, NATIVE_MARKS), count(e.plural, NATIVE_MARKS))))
          problems.push(`placeholders or line breaks differ: ${e.id} -> ${s}`);
      });
    }
    expect(problems).toEqual([]);
  });

  it('translates every page entry and keeps its arguments and tags', () => {
    const { entries } = parse(readFileSync(`${localization}pages/${code}.po`, 'utf8'));
    const english = parse(readFileSync(`${localization}pages/${languages.source}.po`, 'utf8')).entries;
    const ids = new Set(entries.map((e) => `${e.context ?? ''}\u0004${e.id}`));
    const problems: string[] = english.filter((e) => !ids.has(`${e.context ?? ''}\u0004${e.id}`)).map((e) => `missing: ${e.id}`);
    for (const e of entries) {
      const s = e.strings[0];
      if (e.fuzzy) problems.push(`fuzzy: ${e.id}`);
      if (!s) { problems.push(`untranslated: ${e.id}`); continue; }
      let compiled: CompiledMessage;
      try { compiled = compileMessageOrThrow(s); } catch (error) {
        problems.push(`not a valid message: ${e.id} -> ${s} (${error})`);
        continue;
      }
      const tags = (text: string) => new Set([...text.matchAll(PAGE_TAGS)].map((m) => m[0]));
      if (!sameSet(argumentNames(compiled), argumentNames(compileMessageOrThrow(e.id))) || !sameSet(tags(s), tags(e.id)))
        problems.push(`arguments or tags differ: ${e.id} -> ${s}`);
      const missing = missingPluralForms(compiled, tag);
      if (missing.length > 0) problems.push(`plural needs ${missing.join(', ')}: ${e.id} -> ${s}`);
    }
    expect(problems).toEqual([]);
  });
});

it('has a catalog of each kind for every language and no others', () => {
  const codes = languages.languages.map((l) => l.code).sort();
  const pages = readdirSync(`${localization}pages`).filter((f) => f.endsWith('.po')).map((f) => f.slice(0, -3)).sort();
  const native = readdirSync(`${localization}native`).filter((f) => f.endsWith('.po')).map((f) => f.slice(0, -3)).sort();
  expect(pages).toEqual(codes);
  expect(native).toEqual(translated.map((l) => l.code).sort());
});
