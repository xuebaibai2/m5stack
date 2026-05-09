---
name: partial-region-redraw
description: Use when implementing animations, progress indicators, or live UI updates on M5Unified/M5GFX displays; keep redraws scoped to the animated region and avoid full-screen clears unless the screen state changes.
---

# Partial Region Redraw

Use this pattern for embedded display animation:

- Draw the full screen once when the state changes.
- Repaint only the smallest rectangle that contains the moving content.
- Clear that rectangle before drawing the next frame.
- Keep text and static widgets outside the animated area.
- If motion overlaps static content, split the screen into layers or use an offscreen sprite.

Prefer full-screen redraw only for transitions, layout changes, or screens where most pixels change together.
