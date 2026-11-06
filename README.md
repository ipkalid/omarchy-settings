# Settings

![Settings showing the Appearance pane](docs/screenshot.png)

One window for the things you would otherwise change by hand in config files
on [Omarchy](https://omarchy.org). It is laid out like a desktop settings app
(a searchable sidebar of panes; searching for a setting outlines it in its
pane) and drawn like the Omarchy shell's own Wi-Fi and Bluetooth panels, in
the current theme's colors and monospace font. It follows theme, text size,
corner, and language changes live, in all of Omarchy's languages, right to
left included.

| Pane | What's in it |
| --- | --- |
| Wi-Fi | Networks, hidden networks, DNS servers, Wi-Fi band |
| Bluetooth, VPN | Pairing; WireGuard and OpenVPN connections (import, sign in, install support) |
| Power | Power mode now, on the adapter, and on battery; hibernation |
| General | About, updates and update channel, language, time zone and clock, default apps and file types, login items, web apps, storage and snapshots, changes and backup |
| Appearance | Theme gallery, installing and removing themes, corners, transparency, animations, text size, font |
| Desktop & Dock, Bar, Plugins | Dock and pinned apps, window layout and gaps, window rules, focus; the bar's widgets and their order; shell plugins and updates |
| Displays, Wallpaper | Resolution, refresh, scale, rotation, Night Shift and its schedule; wallpapers and adding your own |
| Notifications, Sound | Do Not Disturb, crash reports; output and input devices and volume |
| Lock Screen, Login & Security, Firewall | Idle times; password, fingerprint, security keys; ufw on/off and open ports |
| Keyboard, Mouse, Printers | Repeat, layouts, special keys, backlight, a shortcut editor; pointer and trackpad; printers |

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
- So are your own shortcuts, login items, window rules, and Night Shift's
  schedule (which also writes `~/.config/hypr/hyprsunset.conf`, after
  backing up yours). Every change Settings keeps is listed under General >
  Changes & Backup, with a reset for each.
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
| `Alt+←` `Alt+→`, mouse side buttons | Back and forward |
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
omasettings act shortcut.add "SUPER + E" Files launch org.gnome.Nautilus.desktop
omasettings export ~/Documents           # settings, shortcuts, login items, rules
omasettings import ~/Documents/omarchy-settings-2026-11-06.json
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
