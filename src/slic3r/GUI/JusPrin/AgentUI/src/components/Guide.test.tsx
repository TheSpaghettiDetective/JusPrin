// Help › JusPrin guide…'s window, against the real docs/guide pages it is
// built with, so a page the window cannot show fails here before a release.
import { fireEvent, render, screen, within } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { describe, expect, it, vi } from 'vitest';
import { Guide } from './Guide';
import { guideAreas, guidePages, type GuidePage } from '../guidePages';
import { guideSections, searchGuide } from '../guide';

const pageNamed = (area: string) => guidePages.find((page) => page.area === area)!;

function show(pages = guidePages, openLink = vi.fn()) {
  render(<Guide pages={pages} openLink={openLink} />);
  return { openLink, user: userEvent.setup() };
}

describe('the guide window', () => {
  it('lists every page under docs/guide, in the order of its index', () => {
    expect(guidePages.map((page) => page.area).sort()).toEqual(guideAreas);
    expect(guidePages[0].title).toBeTruthy();
  });

  it.each(guidePages.map((page) => [page.area, page] as const))('%s: shows the page with every picture and none of what only AI reads',
    (_area, page) => {
      show([page]);
      const article = screen.getByRole('main');
      expect(within(article).getByRole('heading', { level: 1 })).toHaveTextContent(page.title);
      expect(article.textContent).not.toContain('ai-description');
      for (const note of page.text.matchAll(/<!-- ai(?:-description)?: ([\s\S]*?) -->/g))
        expect(article.textContent).not.toContain(note[1].slice(0, 40));
      const pictures = [...page.text.matchAll(/^!\[([^\]]+)\]\(([^)]+)\)$/gm)];
      const shown = within(article).queryAllByRole('img');
      expect(shown.map((picture) => picture.getAttribute('alt'))).toEqual(pictures.map((picture) => picture[1]));
      for (const picture of shown) expect(picture.getAttribute('srcset')).toMatch(/ 2x$/);
      expect(article.querySelector('.guide-missing-picture')).toBeNull();
    });

  it('lists the open page’s sections under it and opens one', async () => {
    const { user } = show();
    const page = guidePages[0];
    const nav = screen.getByRole('navigation', { name: 'Guide pages' });
    const last = page.sections[page.sections.length - 1];
    await user.click(within(nav).getByRole('button', { name: last.heading }));
    expect(within(nav).getByRole('button', { name: last.heading })).toHaveAttribute('aria-current', 'location');
    expect(document.getElementById(last.id)).toHaveTextContent(last.heading);
  });

  it('narrows to the sections that hold every typed word', async () => {
    const { user } = show();
    await user.type(screen.getByRole('searchbox', { name: 'Search the guide' }), 'plate');
    const expected = searchGuide(guidePages.flatMap((page) => page.sections), 'plate');
    expect(expected.length).toBeGreaterThan(1);
    expect(screen.getByRole('status')).toHaveTextContent(`Found in ${expected.length} sections`);
    const plate = screen.getByRole('button', { name: /^Settings for one plate/ });
    expect(within(plate).getAllByText('plate', { selector: 'mark' }).length).toBeGreaterThan(0);
    await user.click(plate);
    expect(plate).toHaveAttribute('aria-current', 'location');
    expect(document.getElementById('settings-for-one-plate')).toBeInTheDocument();
  });

  it('says so when nothing mentions the words, and Escape brings the pages back', async () => {
    const { user } = show();
    const field = screen.getByRole('searchbox', { name: 'Search the guide' });
    await user.type(field, 'wipe tower');
    expect(screen.getByRole('status')).toHaveTextContent('Nothing in the guide mentions “wipe tower”.');
    fireEvent.keyDown(field, { key: 'Escape' });
    expect(field).toHaveValue('');
    expect(screen.getByRole('button', { name: guidePages[0].title })).toBeInTheDocument();
  });

  it('follows a link within the guide, and hands a link out of it to the app', async () => {
    const page: GuidePage = {
      area: 'some-area', title: 'Some area', sections: [], text: [
        '<!-- ai-description: Read before saying where things are. -->', '', '# Some area', '',
        'See [the end](#the-end) or [the site](https://github.com/TheSpaghettiDetective/JusPrin) or [a file](file:///etc/hosts).', '',
        '## The end', '', 'Done.', ''].join('\n'),
    };
    page.sections = guideSections(page.area, page.text);
    const { user, openLink } = show([page]);
    await user.click(screen.getByRole('button', { name: 'the end' }));
    expect(screen.getByRole('button', { name: 'The end' })).toHaveAttribute('aria-current', 'location');
    await user.click(screen.getByRole('button', { name: 'the site' }));
    expect(openLink).toHaveBeenCalledWith('https://github.com/TheSpaghettiDetective/JusPrin');
    // Anything else stays text: the page must never navigate away.
    expect(screen.queryByRole('button', { name: 'a file' })).toBeNull();
    expect(screen.getByText('a file')).toBeInTheDocument();
  });

  it('finds a section by any of its words, ignoring case, and none by words it lacks', () => {
    const sections = pageNamed('print-settings').sections;
    expect(searchGuide(sections, 'CLASSIC Back').map((section) => section.heading)).toContain('Classic view: every setting on screen');
    expect(searchGuide(sections, 'classic wipe')).toEqual([]);
    expect(searchGuide(sections, '   ')).toEqual([]);
  });
});
