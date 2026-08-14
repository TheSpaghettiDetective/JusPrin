# JusPrin UI Design System

This is the implementation-facing companion to the visual brand kit. The brand palette defines identity; these semantic tokens define product UI.

## Design-agent contract

1. Produce paired light and dark frames.
2. Use semantic token names rather than raw hex values.
3. Use HarmonyOS Sans SC for UI and the platform system teletype font for technical values.
4. Specify all dimensions in DIP.
5. Reuse the existing native component density and the 4/8/12 DIP radius scale.
6. Show normal, hover, pressed, disabled, focused, success, warning, and error states where relevant.
7. Use the original logo and established SVG icon library. Reserve the gradient for brand surfaces.

## Semantic color tokens

| Token | Light | Dark | Use |
|---|---:|---:|---|
| surface.canvas | #FFFFFF | #17131F | Application background |
| surface.subtle | #F6F2F7 | #211B2A | Sidebars and groups |
| surface.raised | #FFFFFF | #2B2336 | Dialogs and menus |
| text.primary | #17131F | #F6F2F7 | Primary text |
| text.secondary | #5F5767 | #CFC5D3 | Secondary text |
| border.subtle | #E8DFEB | #4B3E57 | Dividers |
| border.strong | #6F4F84 | #7D6A8D | Focus and essential boundaries |
| action.primary | #3A2D64 | #CE84B7 | Primary action |
| action.primary.hover | #4B3A77 | #D99AC5 | Hover |
| action.primary.pressed | #2B214F | #B76CA0 | Pressed |

## Typography

- Page title: 24/30 DIP, Bold
- Section: 18/24 DIP, Bold
- Body and controls: 14/20 DIP, Regular
- Label: 12/16 DIP, Bold
- Dense metadata: 10/14 DIP, Regular

## Geometry

Spacing: 4, 8, 12, 16, 20, 24, 32, 40, 48 DIP. Standard radii: 4 DIP; compact/branded: 8 DIP; window actions: 12 DIP. Larger radii are limited to onboarding and marketing.

## High-fidelity delivery checklist

Include mode, viewport, token references, component variants, keyboard focus, long/localized content, status states, and any explicit exceptions.
