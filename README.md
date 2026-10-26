<h1 align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="./dist/dev.rockorager.monstar-wordmark.svg">
    <source media="(prefers-color-scheme: light)" srcset="./dist/dev.rockorager.monstar-wordmark-light.svg">
    <img alt="Monstar" src="./dist/dev.rockorager.monstar-wordmark-light.svg" width="360">
  </picture>
</h1>

Monstar is a Wayland terminal emulator built on libghostty.

## Features

- Native Wayland with fractional scaling, IME (text-input-v3), activation, and background blur.
- Kitty graphics, clipboard/paste, and drag-and-drop protocols; OSC 8 hyperlinks;
  automatic URI detection; and synchronized output.
- Deep Linux integration: XDG desktop portals, D-Bus notifications, and optional systemd cgroups.
- Non-blocking scrollback search and rectangular block selection.
- Inertial touchpad scrolling with distinct precision/discrete wheel tuning.
- Automatic desktop light/dark theme tracking, bundled color schemes, and live config reload.

## Install

We maintain source-built [`monstar`](https://aur.archlinux.org/packages/monstar)
and prebuilt [`monstar-bin`](https://aur.archlinux.org/packages/monstar-bin) AUR
packages. Both are automatically updated after we tag a release.

We also attach binaries to GH Releases: https://github.com/rockorager/monstar/releases

Install the current development version on Arch Linux from the
[`monstar-git` AUR package](https://aur.archlinux.org/packages/monstar-git):

Build and install Monstar from source into `~/.local`:

```sh
zig build -Doptimize=ReleaseFast install --prefix "$HOME/.local"
```

The command installs the executable, desktop entry, app icon, manual pages,
and bundled themes. Add `$HOME/.local/bin` to `PATH`, then launch Monstar:

```sh
monstar
```

See [Development](#development) for build requirements.

## Usage

Run Monstar without arguments to start `$SHELL`. Monstar falls back to
`/bin/sh` when `SHELL` is unset. Pass `-e` to run a command directly:

```sh
monstar -e fish --login
```

Pass `--` to run a shell expression:

```sh
monstar -- 'git log --oneline | less'
```

Set the working directory, initial size, title, app ID, font, and one-off
configuration overrides with command-line options:

```sh
monstar --working-directory ~/src \
  --title scratch \
  --window-size-chars 100x32 \
  --font Iosevka \
  -o background-opacity=0.95
```

Run `monstar --help`, `man monstar`, or `man 5 monstar` for the complete
command and configuration reference.

## Configuration

Write configuration to `$XDG_CONFIG_HOME/monstar/config`. Use
`~/.config/monstar/config` when `XDG_CONFIG_HOME` is unset. Start with:

```conf
font-family = Iosevka
font-size = 12.5
theme = light:Rose Pine Dawn,dark:Rose Pine
background-opacity = 0.95
window-padding-x = 8
window-padding-y = 6
mouse-scroll-multiplier = precision:1,discrete:3
smooth-scrolling = true
```

Set `font-feature` to turn OpenType features on or off, as in Ghostty; for
example `font-feature = -calt` disables programming ligatures. Repeat it or
use a comma-separated list (`font-feature = -calt, -liga, ss01`).

Set `font-family` to choose the primary font; the default is Fontconfig's
standard `monospace` family. Fontconfig normally selects matching bold and
italic faces and supplies language and emoji fallbacks automatically. Monstar
also bundles Nerd Font symbols.

For explicit control over separate bold, italic, bold-italic, or ordered
fallback families, define a virtual family such as `Monstar Custom` in
`$XDG_CONFIG_HOME/fontconfig/conf.d/50-monstar.conf` (normally
`~/.config/fontconfig/conf.d/50-monstar.conf`), then set
`font-family = Monstar Custom`. An annotated template is installed at
`<prefix>/share/doc/monstar/examples/50-monstar.conf` and lives at
[`dist/50-monstar.conf`](dist/50-monstar.conf) in the source tree.

By default, `background-opacity` affects only the default terminal background
and padding. Set `background-opacity-cells = true` to apply it to explicit cell
background colors too. Monstar requests compositor-provided blur whenever the
background is translucent; set `background-blur = false` to keep it clear.
Blur uses the standard `ext-background-effect-v1` protocol, or KWin's blur
protocol on KDE Plasma versions without it.

Leave `theme` unset to follow the desktop light/dark preference with Monstar's
built-in Fluent 2 color schemes. Load bundled iTerm2 color schemes by name. Put
custom themes in `$XDG_CONFIG_HOME/monstar/themes` or
`~/.config/monstar/themes`.

Set the default command for new windows. Commands run through `/bin/sh -c`
unless prefixed with `direct:`:

```conf
command = fish --login
# command = direct:fish --no-config
```

Command-completion notifications are disabled by default, matching Ghostty:
`notify-on-command-finish = never`, `notify-on-command-finish-action = bell`,
and `notify-on-command-finish-after = 5s`. To show desktop notifications for
commands that finish after more than five seconds while the window is unfocused:

```conf
notify-on-command-finish = unfocused
notify-on-command-finish-action = no-bell,notify
```

Your shell must emit OSC 133 command-start/end markers; Monstar does not inject
shell integration. Focus is checked at completion, and alternate-screen use
does not cancel timing. These settings do not affect explicit OSC 9/777
notifications. See `man 5 monstar` for action flags and duration syntax.

Other settings:

- **app-id** — Wayland app-id and desktop-entry hint for desktop integration
  (default `dev.rockorager.monstar`).
- **env** — Repeatable `NAME=value` child environment entries; `NAME=`
  removes an inherited variable.
- **term** — `TERM` value for the child (default `monstar`). See
  [Terminfo](#terminfo) for remote hosts that lack the entry.
- **title** — Fixed window title; title escape sequences from programs are
  ignored while it is set. `--title` only sets the initial title.
- **window-width / window-height** — Initial terminal grid size in cells.
  Both must be set; values below 10 columns or 4 rows are raised to that
  minimum. `--window-size-chars` sets both; `--window-size-pixels` takes
  precedence.
- **working-directory** — Starting directory for the child: an absolute path,
  `~`, or `~/path`. Unusable directories are ignored with a warning.
  `--working-directory` overrides it, and `Ctrl+Shift+N` windows start in the
  current terminal directory.
- **pipe-command-output** — Shell command that receives the last command's
  output on stdin when `Ctrl+Shift+G` is pressed.
- **scrollback-limit** — Terminal page storage in bytes, including the active
  screen (default `50000000`).
- **image-storage-limit** — Kitty graphics image storage in bytes per screen;
  `0` disables the protocol (default `320000000`).
- **mouse-hide-while-typing** — Hide the pointer while typing; moving or
  clicking shows it again (default `false`).
- **inertial-scrolling** — Whether finger scrolling continues with inertial
  motion after release (default `true`).
- **smooth-scrolling** — Pixel-level scrollback movement: touchpad scrolling
  and inertial motion move by pixels, and mouse wheel steps (including
  high-resolution wheels) are eased over a short animation (default `true`).
  Set to `false` for whole-row scrolling. Scrolling sent to applications stays
  discrete.
- **cursor-style** — Default cursor shape: `block`, `bar`, `underline`, or
  `block_hollow` (default `block`). Programs can still change it with DECSCUSR.
- **cursor-trail** — Neovide-style cursor animation: milliseconds for the
  cursor to glide to its new position, stretching into a trail; `0` turns it
  off (default `0`; try `150`).
- **cursor-trail-size** — How strongly the cursor stretches, from `0` (slides
  without stretching) to `1` (the leading edge jumps, the trailing edge
  follows) (default `0.8`).
- **cursor-style-blink** — Blink the default cursor while focused (default
  `false`). Programs can request blinking with DECSCUSR or DEC mode 12, which
  is now honored.
- **clipboard-trim-trailing-spaces** — Remove trailing whitespace from copied
  lines (default `true`).
- **copy-on-select** — Where finished mouse selections go: `true` (primary
  selection), `clipboard` (primary selection and clipboard), or `false`
  (default `true`).
- **copy-highlight-duration** — Post-copy selection flash in milliseconds;
  `0` disables the flash (default `200`).
- **keybind** — Repeatable Ghostty-style `trigger=action` bindings for line
  scrolling and opening new windows. See [Keybindings](#keybindings) below.
- **background / foreground / cursor-color / cursor-text** — Terminal colors
  as `#RRGGBB` or `RRGGBB`; explicit colors override the theme.
  `cursor-color` and `cursor-text` also accept `cell-foreground` and
  `cell-background` to match the cursor cell at runtime.
- **selection-background / selection-foreground** — Selection colors.
- **copy-highlight / copy-highlight-foreground** — Post-copy flash colors.
- **palette** — Override one palette entry by index `0`–`255`; repeat the key
  for more entries.

Press `Ctrl+Shift+,` or send `SIGUSR1` to reload the configuration:

```sh
pkill -USR1 monstar
```

Visual and interaction settings apply immediately. Process and storage
settings apply to new windows.

## Keybindings

Configure line scrolling and new windows with repeatable `keybind` entries:

```conf
# Default line-scrolling bindings:
keybind = shift+up=scroll_page_lines:-1
keybind = shift+down=scroll_page_lines:1
# Additional shortcuts, with independent line counts:
keybind = ctrl+shift+k=scroll_page_lines:-5
keybind = alt+j=scroll_page_lines:5
# Remove a default shortcut:
keybind = shift+up=unbind
# Move new windows from the default Ctrl+Shift+N:
keybind = ctrl+shift+enter=new_window
keybind = ctrl+shift+n=unbind
```

`new_window` opens an independent window in the current directory.
For `scroll_page_lines`, negative counts scroll up; positive counts scroll down. Counts range from
`-32768` to `32767`. Multiple triggers can invoke the same action; repeating a
trigger replaces its previous binding. Invalid entries leave previous bindings
unchanged. Bindings reload with the configuration.

Triggers combine lowercase `shift`, `ctrl` (`control`), `alt` (`opt`, `option`),
or `super` (`cmd`, `command`) with one key. A single Unicode character such as
`k` or `ö` follows the keyboard layout; names such as `KeyK`, `arrow_up`,
`PageUp`, or `equal` select physical keys. `up`, `down`, `left`, and `right`
are also accepted. Modifiers match exactly (ignoring Caps Lock and Num Lock).
Include Shift when it is held to produce punctuation, for example
`keybind = ctrl+shift++=scroll_page_lines:-5` on a US keyboard.

Configured bindings take precedence over fixed shortcuts below; `unbind`
passes the key to the application instead. Supported actions are
`scroll_page_lines`, `new_window`, `unbind`, and these Ghostty tab and split
actions:

| Action | Default |
| --- | --- |
| `new_tab` | `Ctrl+Shift+T` |
| `close_surface` | `Ctrl+Shift+W` |
| `close_tab` | — |
| `previous_tab` / `next_tab` | `Ctrl+Shift+Left` / `Ctrl+Shift+Right`, `Ctrl+PageUp` / `Ctrl+PageDown`, `Ctrl+Shift+Tab` / `Ctrl+Tab` |
| `goto_tab:N` / `last_tab` | `Alt+1`…`Alt+8` / `Alt+9` |
| `move_tab:N` | `Ctrl+Shift+PageUp` / `Ctrl+Shift+PageDown` (`-1` / `1`) |
| `new_split:right\|down\|left\|up` | `Ctrl+Shift+O` (right), `Ctrl+Shift+E` (down) |
| `goto_split:previous\|next\|up\|down\|left\|right` | `Ctrl+Super+[` / `Ctrl+Super+]`, `Ctrl+Alt+Arrow` |
| `toggle_split_zoom` | `Ctrl+Shift+Enter` |
| `resize_split:DIRECTION,PIXELS` | `Ctrl+Super+Shift+Arrow` (10 pixels) |
| `equalize_splits` | `Ctrl+Super+Shift+=` |

Tab and split actions are performed by a hosting application such as
[monstar-qt](#qt-frontend). In a standalone window their triggers behave as if
unbound and reach the application. Ghostty sequences, binding flags, and other
actions are not yet supported. Search mode retains its own controls.

| Shortcut | Action |
| --- | --- |
| `Ctrl+Shift+C` / `Ctrl+Shift+V` | Copy / paste |
| `Ctrl+Shift+F` | Search scrollback |
| `Ctrl+Shift+N` | Open a new window in the current directory |
| `Ctrl+Shift+,` | Reload configuration |
| `Shift+Up` / `Shift+Down` | Scroll up / down one line |
| `Shift+PageUp` / `Shift+PageDown` | Scroll back / forward one page |
| `Shift+Home` / `Shift+End` | Scroll to the top / bottom of scrollback |
| `Ctrl++` / `Ctrl+=` / `Ctrl+-` | Adjust the font size |
| `Ctrl+0` | Reset the font size |
| `Ctrl` + left click | Open a hyperlink or detected URI |
| `Ctrl` + right click | Copy a hyperlink or detected URI |
| `Ctrl` + drag | Make a rectangular selection |

Writing-key shortcuts follow the active XKB keyboard layout.

Scrollback search updates as you type. Press `Ctrl+N` and `Ctrl+P` to move
between matches. Press `Enter` to copy a match to the primary selection. Press
`Escape` to restore the previous viewport.

Scrollback keybindings pass through to applications using the alternate screen.

Hold `Shift` while dragging to select text after an application captures the
mouse. Hold `Ctrl+Shift` instead of `Ctrl` to open or copy a link in this
state. Middle-click to paste the primary selection.

## Linux integration

- XDG desktop portals open links, local files, and directories. Portal
  settings provide desktop light/dark and reduced-motion preferences. Link
  opening uses Wayland activation tokens.
- D-Bus session services provide desktop notifications and launcher progress.
  Notification actions can activate the terminal window.
- Monstar launches new windows opened with `Ctrl+Shift+N` through the systemd
  user manager. Optional transient scopes place each shell process tree in a
  separate scope.
- Wayland protocols provide fractional scaling, text-input-v3 IME, cursor
  shapes, clipboard and primary selection, server-side decorations, named app
  icons, activation, system bell, and translucent background blur support.

Enable a transient systemd scope for each newly spawned shell with:

```conf
linux-cgroup = always
```

If the session bus or systemd scope is unavailable, Monstar leaves the shell
in its inherited cgroup. Link opening falls back to `xdg-open` when the portal
is unavailable. Monstar disables optional Wayland protocols when the
compositor does not support them.

Monstar speaks the required subset of the D-Bus wire protocol directly, so
D-Bus is not a build or library dependency. Pass `-Ddbus=false` to compile the
client out entirely, at the cost of desktop notifications, launcher progress,
portal-based link/file opening (falls back to `xdg-open`), portal appearance
detection, and systemd cgroup isolation. Nothing else is affected. `systemd`
itself is never a build dependency; it is only used at runtime, over the
session bus, when `linux-cgroup = always` is configured and a systemd user
session is detected.

## Qt frontend

`monstar-qt` shows Monstar terminals as tabs of splits in a Qt window. Each
pane is a full `monstar` process, so every terminal feature and setting works
the same; the tab and split keybindings above become active, and the shell
inside still sees your desktop's `WAYLAND_DISPLAY`.

```sh
monstar-qt
monstar-qt --working-directory ~/src -e fish --login
```

`monstar-qt` accepts `--config`, `-o key=value`, `--working-directory`, `-e`,
and `--` like `monstar`, and passes them on to its terminals. It runs the
`monstar` beside its own executable, then the one on `PATH`; `--terminal PATH`
or `qt-terminal-command` chooses another. It reads these settings from the
same configuration file:

- **window-width**, **window-height** — As in `monstar`: the first terminal
  starts with this grid, and the window fits it plus the title and tab bars.
- **window-decoration** — `client` draws a combined title and tab bar
  (icon, tabs, new tab, minimize, maximize, close); `none` drops the title
  bar; `auto` or `server` keeps the desktop's (default `auto`). Drag empty
  space in the bar to move the window, double-click it to maximize, and drag
  the window edges to resize. Tabs merge into the bar when
  `qt-tabs-location = top`; a single tab shows as the bar's title.
- **qt-window-corner-radius** — Corner radius in pixels of `client` and `none`
  windows, `0`–`64`; the background blur follows the rounded shape. `0` keeps
  square corners (default `8`). Maximized windows are square.
- **qt-tabs-location** — Tab strip edge: `top`, `bottom`, `left`, or `right`
  (default `top`).
- **window-show-tab-bar** — `always`, `auto` (only with two or more tabs), or
  `never` (default `auto`).
- **unfocused-split-opacity** — Opacity of unfocused splits, `0.15`–`1`
  (default `0.7`).
- **unfocused-split-fill** — Color laid over unfocused splits (default the
  `background` color).
- **split-divider-color** — Divider color (default `#3a3a3a`).
- **focus-follows-mouse** — Focus the split under the pointer (default
  `false`).
- **window-inherit-working-directory** — Start new tabs, splits, and windows
  in the focused terminal's directory (default `true`).

Drag a divider to resize splits. Click a tab to select it; middle-click or
press its × to close it; press + for a new tab. `SIGUSR1` reloads these
settings. See [docs/qt-frontend.md](docs/qt-frontend.md) for how it works and
its current limitations.

## Terminfo

Monstar uses `TERM=monstar` by default.

If a remote server does not recognize `monstar` over SSH, send it the installed
definition from your local machine:

```
infocmp -x monstar | ssh YOUR-SERVER tic -x -
```

Alternatively, send the definition from the latest commit:

```
curl -fsSL https://raw.githubusercontent.com/rockorager/monstar/main/dist/monstar.terminfo \
  | ssh YOUR-SERVER tic -x -
```

See [Ghostty's Terminfo](https://ghostty.org/docs/help/terminfo) docs for
details.

## Performance

Reference distributions recorded on an Intel Core Ultra 7 258V. Lower is
better. Results vary by workload.

### Startup

Launch-and-exit time over 100 fresh runs of `true`:

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="./dist/benchmark-startup.svg">
  <source media="(prefers-color-scheme: light)" srcset="./dist/benchmark-startup-light.svg">
  <img alt="Box plots of terminal startup times. Monstar has the lowest median at 12.3 milliseconds." src="./dist/benchmark-startup-light.svg">
</picture>

### Keystroke latency

Key event to echoed-frame commit over 200 injected keys:

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="./dist/benchmark-latency.svg">
  <source media="(prefers-color-scheme: light)" srcset="./dist/benchmark-latency-light.svg">
  <img alt="Box plots of terminal keystroke latency. Monstar has the lowest median at 0.26 milliseconds." src="./dist/benchmark-latency-light.svg">
</picture>

### PTY throughput

Sample-time distributions from eight vtebench workloads:

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="./dist/benchmark-vtebench.svg">
  <source media="(prefers-color-scheme: light)" srcset="./dist/benchmark-vtebench-light.svg">
  <img alt="Grouped box plots comparing Monstar, foot, Ghostty, kitty, and Alacritty across eight vtebench workloads." src="./dist/benchmark-vtebench-light.svg">
</picture>

vtebench measures producer blocking and PTY read throughput, not frame
presentation or input latency.

<details>
<summary>Benchmark methodology</summary>

- Date: July 15, 2026.
- Session: hardware-accelerated, headless Sway 1.12 at 2560×1440.
- Renderer: GLES2 on the Intel GPU through `/dev/dri/renderD128`.
- Process setup: empty configuration and a fresh process for each terminal.
- Startup: measured with hyperfine 1.20.0 after 10 warmups. Commands were
  `monstar -e true`, `foot true`, `ghostty -e true`, `kitty true`, and
  `alacritty -e true`. The result covers the complete process lifetime, not
  time to the first visible frame.
- Keystroke latency: measured in 120×40 terminals under
  `WAYLAND_DEBUG=client`. The test injected 200 keys with `wtype` at intervals
  from 50 to 100 ms. A marked 20 ms raw-PTY echo delay was subtracted from each
  sample to separate output commits from toolkit-only redraws. The result
  includes client key handling, the PTY round trip, rendering, and buffer
  commit. It excludes physical input, compositor presentation, and display
  latency.
- PTY throughput: measured with the default
  [vtebench](https://github.com/alacritty/vtebench) suite at 120×40 cells. The
  test used the standard 1 MiB minimum sample size and 10-second workload
  limit. The result covers PTY reads, not frame rate, display latency, or
  final-frame presentation.
- Versions: Monstar 0.1.0 (`5b350e2`, `ReleaseFast`), foot 1.27.0,
  Ghostty 1.3.2 tip (`c5a21ed`), kitty 0.47.4, and Alacritty 0.17.0.

</details>

## Development

Building requires:

- Zig 0.16
- ncurses (`tic`)
- Wayland 1.25 core schema
- `wayland-protocols` 1.49
- Dev libs for wayland, fontconfig, freeType, harfbuzz, and xkbcommon
- Monstar negotiates protocol versions at runtime to support older compositors

```sh
zig build
zig build fmt
zig build test
```

Build and test `monstar-qt` with Qt 6.8 or newer (QtBase, QtDeclarative, and
QtWayland), CMake, and the Wayland server library. `nix develop .#qt` provides
them:

```sh
cmake -S qt -B qt/build -G Ninja
cmake --build qt/build
ctest --test-dir qt/build
qt/build/monstar-qt --terminal zig-out/bin/monstar
```

Rendering uses exact-sRGB linear-light blending with 8-bit framebuffers and
fractional grayscale text with light hinting and native Adobe CFF stem darkening.
Text coverage is corrected so glyph weight matches encoded-space blending.
No rendering build switch or compositor color management is needed. See
[linear-light blending](docs/linear-light-blending.md) for precision, memory,
and benchmark tradeoffs.

To smoke-test Kitty graphics animation in a running Monstar window:

```sh
python3 scripts/kitty-animation.py
```

The checkerboard image should change colors four times per second for eight
seconds, including while no terminal output or input occurs.
