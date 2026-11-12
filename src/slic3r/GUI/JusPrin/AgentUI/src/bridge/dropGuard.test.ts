// A drag the page does not take must never reach the web view's own drop
// handling, which opens a dropped file or link in place of the page.

import { afterEach, beforeEach, describe, expect, it } from 'vitest';
import { fireEvent } from '@testing-library/react';
import { installDropGuard } from './dropGuard';

describe('the page drop guard', () => {
  let remove: () => void;
  beforeEach(() => { remove = installDropGuard(window); });
  afterEach(() => { remove(); document.body.innerHTML = ''; });

  const files = { files: [new File(['png'], 'photo.png', { type: 'image/png' })], types: ['Files'] };
  const link = { files: [], types: ['text/uri-list', 'text/plain'] };

  it('cancels a file dragged over or dropped where nothing takes it', () => {
    const area = document.body.appendChild(document.createElement('div'));
    expect(fireEvent.dragOver(area, { dataTransfer: files })).toBe(false);
    expect(fireEvent.drop(area, { dataTransfer: files })).toBe(false);
  });

  it('cancels a link dropped outside a text field', () => {
    const area = document.body.appendChild(document.createElement('div'));
    expect(fireEvent.drop(area, { dataTransfer: link })).toBe(false);
  });

  it('leaves a text field its own drop of text or a link', () => {
    const field = document.body.appendChild(document.createElement('textarea'));
    expect(fireEvent.dragOver(field, { dataTransfer: link })).toBe(true);
    expect(fireEvent.drop(field, { dataTransfer: link })).toBe(true);
  });

  it('cancels a file dropped on a text field', () => {
    const field = document.body.appendChild(document.createElement('textarea'));
    expect(fireEvent.drop(field, { dataTransfer: files })).toBe(false);
  });
});
