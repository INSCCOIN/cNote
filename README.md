# cNote

Pocket notes for the SharkDeck (480×320 framebuffer).

```bash
cd /home/working/cNote
rm -f *.o cNote
make
./cNote
./cNote /path/to/file.txt
```

Notes live in `~/notes/` (`scratch.txt` on first run). Settings in `~/.cnote.cfg`.

## Keys

| | |
|--|--|
| Esc | open / close the **File · Edit · Settings** tab menu |
| Tab (in menu) or ← → | switch tabs |
| ↑ ↓ Enter | pick a menu item |
| Tab (in editor) | insert indent spaces |
| Ctrl+S | save |
| Ctrl+O | open (name on the status line) |
| Ctrl+N | new |
| **Ctrl+X** | quit — asks to save if dirty |
| Y / N / Esc | save / discard / stay |

## Settings (tab menu)

- Font: small (1×) or large (2×)
- Color: paper / green / amber / night
- Word wrap on/off
- Tab indent 2 / 4 / 8
