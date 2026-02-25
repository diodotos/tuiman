# Keybindings

This document tracks rewrite keybinding status.

## Implemented in current OpenTUI scaffold

- `j` / `k` and arrow keys: move request selection.
- `/`: placeholder search mode message.
- `Enter`: placeholder action-row message.
- `Esc`: clear transient status/filter.
- `q`: quit.
- `Z` + `Z` currently exits as an early placeholder.

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
- `n` / `N`: next/previous match.
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
- `r`: replay selected run.
- `H` / `L`: resize split.
- `{` / `}`: scroll details.
- `Esc`: return to main.
