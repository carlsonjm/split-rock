---
id: look
title: The desktop's look - light or dark, the global theme, the colours
also:
  - Make it dark
  - Turn on dark mode
  - Change the theme
  - Change the colours
  - The screen is too bright and white
checked: unreleased
format: 1
---

## Look around

```sh
echo "Global theme now: $(kreadconfig6 --file kdeglobals --group KDE --key LookAndFeelPackage)"
echo "Colours now: $(kreadconfig6 --file kdeglobals --group General --key ColorScheme)"
echo "Global themes installed:"
plasma-apply-lookandfeel --list
echo "Colour schemes installed:"
plasma-apply-colorscheme --list-schemes
```

## Change

A global theme sets the whole look at once: colours, window borders, the panel
and the pointer. Prefer it when the person says "dark" or "light" without more:
`plasma-apply-lookandfeel -a <name>`, with a name from the list above, such as
org.kde.breezedark.desktop for dark and org.kde.breeze.desktop for light.

For the colours only, keeping everything else: `plasma-apply-colorscheme <name>`.

Both apply at once to the running desktop. Never edit kdeglobals by hand.

## Undo

Split Rock writes the undo for these two itself, putting back the whole look
the person has now, so leave "undo" out.

## Check

`kreadconfig6 --file kdeglobals --group KDE --key LookAndFeelPackage` for a
global theme, or `--group General --key ColorScheme` for colours.
