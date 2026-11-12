// The app guide as Help › JusPrin guide…'s window carries it: every
// docs/guide page and its pictures, packed into the page when it is built, so
// the window needs no files of its own beside the page. Packing them adds
// what the pictures weigh to every window that loads the page; measured on
// 2026-11-11, one guide page's pictures took the page from 795 KB to
// 1.28 MB, and WebKit on a Mac opened it in 24 ms instead of 22.
import { guideSections, guideTitle, type GuideSection } from './guide';

const pages = import.meta.glob('../../../../../../docs/guide/*.md', { eager: true, query: '?raw', import: 'default' }) as
  Record<string, string>;
const pictureFiles = import.meta.glob('../../../../../../docs/guide/pictures/**/*.png', { eager: true, query: '?url', import: 'default' }) as
  Record<string, string>;

const PREFIX = '../../../../../../docs/guide/';

export interface GuidePage {
  area: string;
  title: string;
  text: string;
  sections: GuideSection[];
}

const pageText = (area: string) => pages[`${PREFIX}${area}.md`];

// The pages in the order the guide's index lists them.
export const guidePages: GuidePage[] = [...(pageText('README') ?? '').matchAll(/^- \[[^\]]+\]\(([a-z0-9-]+)\.md\)$/gm)]
  .map((match) => match[1])
  .filter((area) => pageText(area) !== undefined)
  .map((area) => ({ area, title: guideTitle(pageText(area)), text: pageText(area), sections: guideSections(area, pageText(area)) }));

// Every page under docs/guide, listed in the index or not, so a test can
// tell a page the window leaves out.
export const guideAreas: string[] = Object.keys(pages).map((path) => path.slice(PREFIX.length, -'.md'.length))
  .filter((area) => area !== 'README').sort();

// A picture as a page names it, "pictures/<page>/<name>.png".
export function guidePicture(src: string): string | undefined {
  return pictureFiles[`${PREFIX}${src}`];
}
