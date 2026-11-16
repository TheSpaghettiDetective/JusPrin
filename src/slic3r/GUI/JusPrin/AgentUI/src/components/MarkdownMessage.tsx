import { memo, type ReactNode } from 'react';
import Markdown, { type Components } from 'react-markdown';
import remarkGfm from 'remark-gfm';
import { t } from '@lingui/core/macro';

interface Props {
  children: string;
  streaming?: boolean;
}

// Messages may contain model-generated URLs and images. Keep them inert until
// the native host provides an explicit, policy-controlled way to open external
// content; navigating this local WebView would replace the Agent page.
// An image in a reply, which the page never loads, by its description.
function imageText(description: string | undefined): string {
  return description ? t`[Image: ${description}]` : t`[Image: description unavailable]`;
}

const components: Components = {
  a: ({ children, href }) => (
    <span className="markdown-link" title={href}>
      {children}
    </span>
  ),
  img: ({ alt }) => <span className="markdown-image-alt">{imageText(alt)}</span>,
  // The frame owns the stroke and the sideways scroll, so the table itself
  // can fill the reply's width.
  table: ({ children }) => (
    <div className="markdown-table">
      <table>{children}</table>
    </div>
  ),
};

const remarkPlugins = [remarkGfm];

export const MarkdownMessage = memo(function MarkdownMessage({ children, streaming = false }: Props): ReactNode {
  const className = streaming ? 'markdown-content streaming-cursor' : 'markdown-content';

  return (
    <div className={className}>
      <Markdown components={components} remarkPlugins={remarkPlugins} skipHtml>
        {children}
      </Markdown>
    </div>
  );
});
