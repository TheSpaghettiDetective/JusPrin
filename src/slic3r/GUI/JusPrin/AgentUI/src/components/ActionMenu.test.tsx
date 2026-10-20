// The menu control behind the header's ⋯ menu and the composer's +, model and
// thinking menus.

import { fireEvent, render, screen } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { describe, expect, it, vi } from 'vitest';
import { ActionMenu } from './ChatNavigation';

describe('a menu', () => {
  // In the app's web view (WebKit on macOS) a click does not focus a button,
  // so pressing a row the keyboard focus is not on used to blur the focused
  // row to nothing, close the menu, and lose the click: picking another model
  // did nothing. A press inside the menu must leave the focus where it is.
  it('keeps the focus where it is when a row is pressed', async () => {
    const onSecond = vi.fn();
    render(<ActionMenu label="Chat actions" items={[
      { label: 'Rename', onSelect: vi.fn() },
      { label: 'Delete', onSelect: onSecond },
    ]} />);
    await userEvent.click(screen.getByRole('button', { name: 'Chat actions' }));
    expect(screen.getByRole('menuitem', { name: 'Rename' })).toHaveFocus();
    // fireEvent returns false when the press's default, moving the focus,
    // was cancelled.
    expect(fireEvent.mouseDown(screen.getByRole('menuitem', { name: 'Delete' }))).toBe(false);
  });
});
