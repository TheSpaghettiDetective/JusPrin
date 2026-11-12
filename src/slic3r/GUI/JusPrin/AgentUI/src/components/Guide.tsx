// Help › JusPrin guide…'s window (canvas "JusPrin v2", page "JusPrin guide
// window"): the guide's pages at the left with Search the guide above them,
// the open page at the right. Typing narrows the left to the sections that
// hold every typed word; picking one opens its page there.
import { Children, isValidElement, useEffect, useRef, useState, type KeyboardEvent, type ReactNode } from 'react';
import Markdown, { type Components } from 'react-markdown';
import remarkGfm from 'remark-gfm';
import { guideReaderText, headingId, searchGuide, searchSnippet, searchWords, type GuideSection } from '../guide';
import { guidePicture, type GuidePage } from '../guidePages';

interface Props {
  pages: GuidePage[];
  // A link out of the guide, which the app opens in the browser.
  openLink: (url: string) => void;
}

function textOf(children: ReactNode): string {
  return Children.toArray(children).map((child) => {
    if (typeof child === 'string' || typeof child === 'number') return String(child);
    return isValidElement<{ children?: ReactNode }>(child) ? textOf(child.props.children) : '';
  }).join('');
}

// The typed words in a line of text, marked.
function Marked({ text, words }: { text: string; words: string[] }) {
  if (words.length === 0) return <>{text}</>;
  const pattern = new RegExp(`(${words.map((word) => word.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')).join('|')})`, 'gi');
  return <>{text.split(pattern).map((part, index) => (index % 2 === 1 ? <mark key={index}>{part}</mark> : part))}</>;
}

const remarkPlugins = [remarkGfm];

export function Guide({ pages, openLink }: Props) {
  const [query, setQuery] = useState('');
  const [area, setArea] = useState(pages[0]?.area ?? '');
  // The section last picked, and a request to bring it to the top.
  const [chosen, setChosen] = useState<{ area: string; id: string } | null>(null);
  const [scroll, setScroll] = useState(0);
  const main = useRef<HTMLElement>(null);
  const page = pages.find((entry) => entry.area === area) ?? pages[0];
  const words = searchWords(query);
  const found = words.length > 0 ? searchGuide(pages.flatMap((entry) => entry.sections), query) : [];

  useEffect(() => {
    const view = main.current;
    if (!view || scroll === 0) return;
    const heading = chosen?.id ? view.querySelector(`[id="${chosen.id}"]`) : null;
    if (heading) heading.scrollIntoView({ block: 'start' });
    else view.scrollTop = 0;
  }, [scroll]); // eslint-disable-line react-hooks/exhaustive-deps

  const open = (nextArea: string, id: string) => {
    setArea(nextArea);
    setChosen({ area: nextArea, id });
    setScroll((count) => count + 1);
  };

  const onKeyDown = (event: KeyboardEvent<HTMLInputElement>) => {
    if (event.key === 'Escape' && query) {
      event.preventDefault();
      setQuery('');
    }
  };

  const isChosen = (section: GuideSection) => chosen?.area === section.area && chosen.id === section.id;

  const components: Components = {
    h2: ({ children }) => <h2 id={headingId(textOf(children))}>{children}</h2>,
    img: ({ src, alt }) => {
      const url = src ? guidePicture(src) : undefined;
      // Each picture is taken at twice the screen's size, so 2x shows it at
      // the size it has on screen; the column narrows a wider one.
      return url ? <img className="guide-picture" src={url} srcSet={`${url} 2x`} alt={alt ?? ''} />
        : <span className="guide-missing-picture">{alt}</span>;
    },
    a: ({ href = '', children }) => {
      const within = href.match(/^#(.+)$/);
      const other = href.match(/^([a-z0-9-]+)\.md(?:#(.+))?$/);
      const go = within ? () => open(page.area, within[1])
        : other && pages.some((entry) => entry.area === other[1]) ? () => open(other[1], other[2] ?? '')
          : /^https:\/\//.test(href) ? () => openLink(href) : null;
      return go ? <button type="button" className="link guide-link" onClick={go}>{children}</button> : <span>{children}</span>;
    },
  };

  return (
    <div className="guide" data-testid="guide">
      <nav className="guide-nav" aria-label="Guide pages">
        <label className="field-search guide-search">
          <span className="jp-icon jp-icon-search" aria-hidden="true" />
          {/* The Mac's autocorrect would rewrite the words being searched for. */}
          <input type="search" placeholder="Search the guide" aria-label="Search the guide" value={query}
            autoCapitalize="off" autoCorrect="off" spellCheck={false}
            onChange={(event) => setQuery(event.target.value)} onKeyDown={onKeyDown} />
          {query && (
            <button type="button" className="guide-clear" aria-label="Clear the search" title="Clear the search"
              onClick={() => setQuery('')}>
              <span className="jp-icon jp-icon-close" aria-hidden="true" />
            </button>
          )}
        </label>
        {words.length === 0 ? (
          <div className="guide-pages">
            {pages.map((entry) => (
              <div key={entry.area} className="guide-page-group">
                <button type="button" className="guide-page"
                  aria-current={entry.area === page.area && !chosen?.id ? 'page' : undefined}
                  onClick={() => open(entry.area, '')}>{entry.title}</button>
                {entry.area === page.area && entry.sections.slice(1).map((section) => (
                  <button type="button" key={section.id} className="guide-section"
                    aria-current={isChosen(section) ? 'location' : undefined}
                    onClick={() => open(section.area, section.id)}>{section.heading}</button>
                ))}
              </div>
            ))}
          </div>
        ) : found.length === 0 ? (
          <div className="guide-nothing" role="status">
            <p className="guide-nothing-title">Nothing in the guide mentions “{query.trim()}”.</p>
            <p>Check the spelling or try other words. You can also ask AI: choose Ask AI… in the Help menu.</p>
          </div>
        ) : (
          <div className="guide-results">
            <p className="guide-count" role="status">
              {found.length === 1 ? 'Found in 1 section' : `Found in ${found.length} sections`}
            </p>
            {found.map((section) => {
              const title = pages.find((entry) => entry.area === section.area)?.title ?? '';
              return (
                <button type="button" key={`${section.area}#${section.id}`} className="guide-result"
                  aria-current={isChosen(section) ? 'location' : undefined}
                  onClick={() => open(section.area, section.id)}>
                  <span className="guide-result-heading">{section.heading}</span>
                  <span className="guide-result-page">{section.id ? title : 'Start of the page'}</span>
                  <span className="guide-result-text"><Marked text={searchSnippet(section, query)} words={words} /></span>
                </button>
              );
            })}
          </div>
        )}
      </nav>
      <main className="guide-content" ref={main}>
        {page && (
          <article className="guide-article">
            <Markdown components={components} remarkPlugins={remarkPlugins} skipHtml>
              {guideReaderText(page.text)}
            </Markdown>
          </article>
        )}
      </main>
    </div>
  );
}
