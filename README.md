# Settings

![Settings showing the Appearance pane](docs/screenshot.png)

One window for the things you would otherwise change by hand in config files
on [Omarchy](https://omarchy.org): Wi-Fi, Bluetooth, sound, displays, the
theme and wallpaper, the bar and dock, idle and lock times, keyboard layouts
and repeat, mouse and trackpad, default apps, language, and time zone. It is
laid out like a desktop settings app (a searchable sidebar of panes) and
drawn like the Omarchy shell's own Wi-Fi and Bluetooth panels, in the current
theme's colors and monospace font. It follows theme, text size, corner, and
language changes live, in all of Omarchy's languages, right to left included.

## How it changes things

Settings never rewrites a file you wrote.

- Where Omarchy has a command for something (`omarchy theme set`,
  `omarchy hyprland corners`, `omarchy bar position`, `omarchy toggle ...`),
  Settings runs that command, so it behaves exactly as Omarchy's menus do.
- Hyprland options (keyboard, mouse, trackpad, gaps, focus, displays) go into
  one generated file, `~/.config/hypr/omasettings.lua`, loaded after your own
  config. After every change Settings reloads Hyprland and checks
  `hyprctl configerrors`; if Hyprland rejects the change, it is rolled back.
  Delete a line there (or run `omasettings reset <id>`) to hand that option
  back to your own files. The choices behind the file are kept in
  `~/.config/omarchy/omasettings.json`.
- Idle and lock times are written into `~/.config/omarchy/shell.json` in
  place, keeping its formatting. A file with a syntax error is left alone.
- Display changes undo themselves after 15 seconds unless you keep them, and
  the last display can't be turned off.
- Interactive setup (passwords, fingerprint, security keys, updates) opens in
  a terminal, the way Omarchy's menus do it.

Everything is written atomically (beside the target, then renamed), and every
change can be undone with Ctrl+Z.

## Keyboard

| Key | Does |
| --- | --- |
| `?` | Show every shortcut |
| `Ctrl+F` or `/` | Search settings |
| `↑` `↓` | Move between panes in the sidebar |
| `Ctrl+↑` `Ctrl+↓` | Previous or next pane from anywhere |
| `Tab`, `Space` | Move between controls, toggle or press the focused one |
| `Alt+←` `Alt+→` | Back and forward |
| `Esc` | Clear the search, or go back |
| `Ctrl+Z`, `Ctrl+Shift+Z` | Undo and redo |
| `Ctrl+R` | Reload what the pane shows |
| `Q` | Quit |

## For scripts and agents

Every setting in the window can be read and changed without a display:

```sh
omasettings list --json                 # every setting: type, range, choices, value
omasettings get keyboard.repeatRate
omasettings set keyboard.repeatRate 45 --json
omasettings reset keyboard.repeatRate   # back to your own config
omasettings data monitors --json        # wifi, bluetooth, sinks, monitors, themes, ...
omasettings open displays               # open the window on a pane
omasettings skill install               # a short skill so agents know how
```

Commands exit 0 on success and 1 on failure, with the reason on stderr.

## Build

Everything it needs is part of Omarchy.

```sh
bin/build      # build/omasettings
bin/test       # the Qt Test suite, offscreen
bin/install    # makepkg -fsi, so it shows up in the app launcher (Super + Space)
```

## License

MIT. The theme and translator code follows Omacut and Monologue by David
Heinemeier Hansson.
