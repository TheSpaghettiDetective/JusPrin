// The app guide. Each docs/guide/<area>.md is a page a person reads on GitHub
// and the source of the skill the model reads: skills/guide-<area>/SKILL.md is
// built from it here and checked in, so everything that reads the skills
// folder (skills.ts, the app's tools test, the wording guard) takes it as any
// other skill. guide.test.ts fails when a page and its skill differ, and
// rewrites the skills when run with JUSPRIN_UPDATE_GUIDE_SKILLS=1.
//
// A page is written for the person. Three things in it are for the model:
// - its first line, <!-- ai-description: … -->, is the line skill_read lists;
// - <!-- ai: … --> holds a note only the model gets, hidden on the page;
// - a line that is only a picture is left out of the skill.

const DESCRIPTION = /^<!-- ai-description: (.+) -->\n/;
const AI_NOTE = /<!-- ai: ([\s\S]*?) -->/g;
const PICTURE_LINE = /^!\[[^\]]*\]\([^)]*\)\n/gm;
const LINK_WITHIN_PAGE = /\[([^\]]+)\]\(#[^)]*\)/g;

export function guideSkillFile(area: string, page: string): string {
  const text = page.replace(/\r\n/g, '\n');
  const description = text.match(DESCRIPTION);
  if (!description) throw new Error(`${area}: a guide page starts with <!-- ai-description: … -->`);
  const body = text.slice(description[0].length)
    .replace(PICTURE_LINE, '')
    .replace(AI_NOTE, '$1')
    .replace(LINK_WITHIN_PAGE, '$1')
    .replace(/\n{3,}/g, '\n\n')
    .trim();
  // Any other comment would reach the model as text.
  if (body.includes('<!--')) throw new Error(`${area}: only ai-description and ai comments are understood`);
  return `---\nname: guide-${area}\ndescription: ${description[1]}\n---\n\n${body}\n`;
}

// The on-screen words a page quotes: bold is kept for those alone.
export function guideLabels(page: string): string[] {
  return [...new Set([...page.matchAll(/\*\*([^*]+)\*\*/g)].map((match) => match[1]))];
}

// Whether `source` has `label` as a whole string or as a whole piece of page text.
export function hasLabel(source: string, label: string): boolean {
  const escaped = label.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  return new RegExp(`["'\`>]\\s*${escaped}\\s*["'\`<]`).test(source);
}

// ---- Help › JusPrin guide…'s window ----------------------------------------
// The window shows a page as a person reads it on GitHub: without its first
// line and its notes for the model, and with its pictures.

export function guideReaderText(page: string): string {
  return page.replace(/\r\n/g, '\n').replace(DESCRIPTION, '').replace(AI_NOTE, '').replace(/\n{3,}/g, '\n\n').trim();
}

// The page's title: its one # heading.
export function guideTitle(page: string): string {
  return page.match(/^# (.+)$/m)?.[1].trim() ?? '';
}

// The id GitHub gives a heading, so a link within a page works in both places.
export function headingId(heading: string): string {
  return heading.trim().toLowerCase().replace(/[^\p{L}\p{N}\s-]/gu, '').replace(/\s/g, '-');
}

// What a person reads of a stretch of the page: no markup, no pictures.
function readable(markdown: string): string {
  return markdown
    .replace(PICTURE_LINE, '')
    .replace(/\*\*([^*]+)\*\*/g, '$1')
    .replace(/\[([^\]]+)\]\([^)]*\)/g, '$1')
    .replace(/^\s*(?:\d+\.|-)\s+/gm, '')
    .replace(/\s+/g, ' ')
    .trim();
}

export interface GuideSection {
  area: string;
  // The page's title for the part before its first ## heading.
  heading: string;
  // Empty for the start of the page.
  id: string;
  text: string;
}

// A page split where a person sees it split: the start under the title, then
// each ## section.
export function guideSections(area: string, page: string): GuideSection[] {
  const text = guideReaderText(page);
  const title = guideTitle(text);
  const parts = text.split(/^## /m);
  const start = parts[0].replace(/^# .+$/m, '');
  return [
    { area, heading: title, id: '', text: readable(start) },
    ...parts.slice(1).map((part) => {
      const [heading, ...rest] = part.split('\n');
      return { area, heading: heading.trim(), id: headingId(heading), text: readable(rest.join('\n')) };
    }),
  ];
}

export function searchWords(query: string): string[] {
  return query.toLowerCase().split(/\s+/).filter(Boolean);
}

// The sections that have every typed word in their heading or text, ignoring
// case, in the order of the pages and of the sections in them.
export function searchGuide(sections: GuideSection[], query: string): GuideSection[] {
  const words = searchWords(query);
  if (words.length === 0) return [];
  return sections.filter((section) => {
    const haystack = `${section.heading} ${section.text}`.toLowerCase();
    return words.every((word) => haystack.includes(word));
  });
}

// The first sentence of a section that holds a typed word, or its first one.
export function searchSnippet(section: GuideSection, query: string): string {
  const words = searchWords(query);
  const sentences = section.text.split(/(?<=[.!?])\s+/);
  return sentences.find((sentence) => words.some((word) => sentence.toLowerCase().includes(word))) ?? sentences[0] ?? '';
}
