# AI Builder

Describe a part of a level in plain English and Claude builds it in the editor.

## Setup
1. Get an API key at [console.anthropic.com](https://console.anthropic.com/settings/keys). API usage is billed to that Anthropic account. It is separate from a Claude Pro or Max subscription.
2. Open any level in the editor and press the green star button next to undo/redo.
3. Press **Account**, paste your key and press **Link**.

## Building
Type what you want and press **Build**. For example:
- *"a 1x speed cube section with three spike jumps, then a ship portal into a short ship corridor"*
- *"a grassy cube part with clouds in the background, and turn the background dark blue when the ship starts"*
- *"three platforms that move up and down while you jump across them"*

Claude can place blocks, hazards, pads, orbs and portals, plus decoration and triggers (colour, move, alpha, toggle, pulse, spawn, rotate, follow, shake, stop). Groups used by a build are always given unused group numbers, so they don't interfere with the rest of your level. The objects appear starting at the middle of your screen and are left selected, so you can move them or delete them straight away.

Pick the model, the effort level and the object limit in the mod settings.
