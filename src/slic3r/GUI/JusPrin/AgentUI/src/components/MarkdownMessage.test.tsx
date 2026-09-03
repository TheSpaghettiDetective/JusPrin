import { render, screen } from '@testing-library/react';
import { describe, expect, it } from 'vitest';
import { MarkdownMessage } from './MarkdownMessage';

describe('MarkdownMessage', () => {
  it('renders GitHub-flavored tables', () => {
    render(
      <MarkdownMessage>{`| Setting | Value |
| --- | --- |
| Walls | 4 |`}</MarkdownMessage>,
    );

    expect(screen.getByRole('table')).toHaveTextContent('Setting');
    expect(screen.getByRole('table')).toHaveTextContent('Walls');
  });

  it('keeps external content inert inside the local WebView', () => {
    const { container } = render(
      <MarkdownMessage>{`[Guide](https://example.com) ![remote preview](https://example.com/image.png)

<script>alert('unsafe')</script>`}</MarkdownMessage>,
    );

    expect(container.querySelector('a')).toBeNull();
    expect(container.querySelector('img')).toBeNull();
    expect(container.querySelector('script')).toBeNull();
    expect(screen.getByText('Guide')).toHaveAttribute('title', 'https://example.com');
    expect(screen.getByText('[Image: remote preview]')).toBeInTheDocument();
  });

  it('keeps incomplete streamed Markdown readable and formats it when completed', () => {
    const { container, rerender } = render(<MarkdownMessage streaming>Use **four walls</MarkdownMessage>);

    expect(screen.getByText('Use **four walls')).toBeInTheDocument();
    expect(container.querySelector('strong')).toBeNull();

    rerender(<MarkdownMessage streaming>Use **four walls**</MarkdownMessage>);
    expect(container.querySelector('strong')).toHaveTextContent('four walls');
  });
});
