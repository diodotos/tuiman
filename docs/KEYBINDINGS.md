# Keybindings

This document tracks rewrite keybinding status.

## Implemented in current OpenTUI scaffold

- `j` / `k` and arrow keys: move request selection.
- `gg` / `G`: jump to top/bottom.
- `/` and `?`: search mode with filter-as-you-type and `Enter` to lock.
- `:`: command mode.
- `Enter`: action row mode.
- `d`: delete confirmation mode (`y` confirm, `n`/`Esc` cancel).
- `ZZ` / `ZQ`: quit from normal mode.
- `H` / `L`: nudge main vertical split.
- `K` / `J`: nudge main response split.
- Mouse drag on divider lines resizes splits; clicking near divider also snaps start for drag-poor terminals.
- `Esc`: clear transient status/filter or cancel active mode.
- `{` / `}`: request body scroll.
- `[` / `]`: response body scroll.

Action row currently wired:

- `y`: sends selected request via TypeScript service and records run history.
- `e`: opens body in `$VISUAL`/`$EDITOR`, then validates + formats JSON-like body before save.
- `a`: opens editor focused on auth fields.
- `Esc` or `n`: cancel action mode.

Command mode currently wired:

- `:new [METHOD] [URL]`: open editor for new request.
- `:edit`: open editor for selected request.
- `:history`: open history screen.
- `:help`: open help screen.
- `:q`: quit.
- `:export [DIR]`: export requests and scrub secret refs.
- `:import <DIR>`: import requests from export folder.

Editor screen currently wired:

- `j` / `k`: move between fields.
- `h` / `l` on Method: cycle method.
- `i` or `Enter`: insert mode for editable fields.
- `Esc`: cancel editor and return.
- `Ctrl+s`: save.
- `:w`, `:q`, `:wq`: editor command mode.
- `:secret VALUE`: store Keychain secret for current `Secret Ref`.
- `e`: open draft body in external editor.

History screen currently wired:

- `j` / `k`: move through runs.
- `/`: history filter mode (live filter while typing; `Enter` lock, `Esc` cancel/restore).
- `r`: replay selected run.
- `H` / `L`: resize split.
- `K` / `J`: resize request/response subwindow ratio in right detail pane.
- Mouse drag on divider resizes list/detail split.
- `{` / `}`: scroll request body in right detail pane.
- `[` / `]`: scroll response body in right detail pane.
- `Esc`: return to main.

## Target parity keybindings (must match C line)

### Main screen

- `j` / `k`: move selection.
- `gg` / `G`: jump top/bottom.
- `Enter`: open action row for selected request.
- `E`: open full editor for selected request.
- `d`: delete selected request with confirmation.
- `ZZ` or `ZQ`: quit from main mode.
- `Esc`: clear filter/status transient state.
- `H` / `L`: nudge vertical divider.
- `K` / `J`: nudge horizontal divider.
- `{` / `}`: scroll request preview body.
- `[` / `]`: scroll response body.

Search/command:

- `/`: forward filter input.
- `?`: reverse/fuzzy search mode.
- `:`: command mode.

### Action row

- `y`: send request.
- `e`: edit body in external editor.
- `a`: auth-focused editor.
- `Esc` or `n`: cancel.

### New request editor

- `j` / `k`: move fields.
- `h` / `l` on method: cycle method.
- `i` or `Enter`: insert mode on field (except method).
- `e`: external body editor.
- `{` / `}`: scroll preview body.
- `:`: editor command line.
- `Ctrl+s`: save.
- `Esc`: cancel/return.

### History screen

- `j` / `k`: move runs.
- `/`: filter runs.
- `r`: replay selected run.
- `H` / `L`: resize split.
- `K` / `J`: resize request/response split in detail pane.
- `{` / `}`: scroll request body.
- `[` / `]`: scroll response body.
- `Esc`: return to main.
