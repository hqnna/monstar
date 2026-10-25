# Qt frontend

`monstar-qt` (in [`qt/`](../qt)) puts Monstar terminals in a Qt window with
tabs and splits. This note records how it is built, why, and what does not
work yet.

## Design

Monstar is a Wayland client end to end: its App owns one terminal, one PTY,
and one `xdg_toplevel`, and renders into `wl_shm` buffers. Rather than
splitting that into a toolkit-neutral library, `monstar-qt` is a small
**nested Wayland compositor** built with Qt Quick and QtWaylandCompositor:

- Each pane is an unmodified `monstar` process. The host creates a socket
  pair, adds one end as a Wayland client, and passes the other to the child
  as `WAYLAND_SOCKET`, so it knows exactly which client belongs to which
  pane and never advertises a listening socket. Monstar drops
  `WAYLAND_SOCKET` from its shell's environment, so programs started in a
  pane still use the desktop's `WAYLAND_DISPLAY`.
- `TerminalHost` owns the tabs. Each tab has a `SplitTree`, a binary tree of
  panes that computes pane rectangles and dividers and answers directional
  navigation, zoom, resize, and equalize. Each pane's `xdg_toplevel` is
  configured maximized at its rectangle, and QML shows it with a
  `ShellSurfaceItem`.
- Key bindings stay in the terminal, which already matches Ghostty triggers
  against the active keyboard layout. When `monstar_host_v1`
  ([protocol](../protocol/monstar-host-v1.xml)) is advertised, Monstar sends
  tab and split actions, and `new_window`, to the host as Ghostty action
  strings with its OSC 7 directory. Elsewhere those triggers pass through.
- `KeyboardForwarder` binds its own `wl_keyboard` on the desktop seat and
  replays the desktop keymap, keys, modifiers, and repeat rate to the
  focused terminal, and drops Qt's translated key events. Qt's compositor
  would otherwise compile a keymap from XKB names and lose the desktop
  layout, groups, and custom or virtual keymaps.
- The clipboard is bridged with the desktop through Qt's retained selection
  and `QClipboard`.

The alternatives were a libghostty-style core library with a Qt "apprt"
(Monstar's App, Window, Clipboard, and input code would all need a toolkit
abstraction — a large rewrite) or embedding terminal surfaces as Wayland
subsurfaces of the Qt window (two event loops sharing one connection, and
keyboard focus belongs to the parent surface). The nested compositor keeps
the terminal unchanged and isolates each pane in its own process.

## Limitations

- Fractional scaling: QtWaylandCompositor offers only integer output scales.
  Terminals render at the next integer scale and Qt scales the result, so
  text can be slightly soft at, for example, 150%.
- Primary selection: QtWaylandCompositor does not implement it, so
  middle-click paste and copying to the primary selection do nothing inside
  `monstar-qt`. The clipboard works.
- Cursor shapes: the compositor has no `cursor-shape-v1`, so Monstar falls
  back to drawing cursors from its cursor theme.
- Desktop integration that Monstar requests per window — activation tokens,
  background blur, the system bell, and toplevel icons — is not forwarded.
- One window per process: `new_window` starts another `monstar-qt`.
- Rendering goes through one extra copy (shm buffer to a GL texture).

## Testing

`ctest --test-dir qt/build` runs the `SplitTree` and settings tests. For
interactive testing without touching the desktop, run a headless Sway
(`WLR_BACKENDS=headless`, `WLR_RENDERER=gles2`) and drive it with `wtype` and
`grim`, as for Monstar itself.
