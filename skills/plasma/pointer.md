---
id: pointer
title: The pointer - its style, and a pointer that is easier to see
also:
  - Make the mouse pointer bigger
  - I can't see the cursor
  - Change the cursor
  - Make the pointer easier to see
checked: unreleased
format: 1
---

## Look around

```sh
echo "Pointer theme now: $(kreadconfig6 --file kcminputrc --group Mouse --key cursorTheme)"
echo "Pointer size now: $(kreadconfig6 --file kcminputrc --group Mouse --key cursorSize)"
echo "Pointer themes installed:"
for folder in /usr/share/icons/* "$HOME/.icons"/* "$HOME/.local/share/icons"/*; do
  [ -d "$folder/cursors" ] && basename "$folder"
done | sort -u
```

## Change

`plasma-apply-cursortheme <name>`, with a name exactly as listed above. It
applies at once. An empty theme setting means the default, breeze_cursors.

"Easier to see" usually means a darker or larger pointer on a light screen, or
a lighter one on a dark screen: choose from what is installed, and say which
and why. If nothing installed fits, say so; don't name a theme that isn't
listed.

## Undo

Split Rock writes the undo itself, putting back the pointer and the rest of
the look the person has now, so leave "undo" out.

## Check

`kreadconfig6 --file kcminputrc --group Mouse --key cursorTheme`
