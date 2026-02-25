# Keybindings

## Main screen

- `j` / `k`: move selection.
- `gg` / `G`: jump top/bottom.
- `Enter`: open action row for selected request.
- `E`: open full editor for selected request (`name`, `method`, `url`, headers, auth, body).
- `d`: delete selected request with confirmation prompt.
- `Esc`: clear filter or clear transient bottom status back to idle hint.
- `H` / `L`: nudge vertical divider left/right.
- `K` / `J`: nudge horizontal divider up/down.
- `{` / `}`: scroll request preview body up/down.
- `[` / `]`: scroll response body up/down.

Search/command:

- `/`: forward filter input.
- `?`: reverse/fuzzy placeholder input (currently same match engine as `/`).
- `n` / `N`: next/previous selection.
- `:`: command mode.

Action row (`Enter` from main):

- `y`: send selected request.
- `e`: edit request body in `$VISUAL`/`$EDITOR`.
  - If body starts with `{` or `[`, `tuiman` validates JSON and pretty-formats on save.
  - Invalid JSON is rejected with an inline error and the previous body is kept.
- `a`: open selected request in auth-focused editor.
- `Esc` or `n`: cancel action row.

Response pane notes:

- Response pane updates after `y` send.
- Shows request/method/url, timestamp, status, duration, error (if any), and wrapped body preview.
- Response body preview stores full response text and is scrollable with `[` / `]`.
- Click-and-hold mouse button 1 on a divider, then drag to resize panes.
- If your terminal does not emit drag events, clicking near a divider still snaps it incrementally.

Delete confirmation prompt:

- `y`: confirm delete.
- `n` or `Esc`: cancel delete.

## New request editor

Normal mode:

- `j` / `k`: move between fields.
- `h` / `l` on Method: cycle HTTP method.
- `i` or `Enter`: edit selected field (insert mode), except `Method`.
- `e`: edit body in external editor.
- `{` / `}`: scroll preview body up/down.
- `:`: editor command line.
- `Ctrl+s`: save request.
- `Esc`: cancel editor.
- mouse drag on the vertical divider: resize editor field/preview panes.

Method field behavior:

- Method cannot be typed directly in insert mode.
- Use `h` / `l` to cycle methods.

Insert mode:

- Type to edit current field.
- `Backspace`: delete character.
- `Option+Backspace`: delete previous word.
- `Enter`: apply and return to normal mode.
- `Esc`: return to normal mode.

Editor command mode (`:`):

- `:w` save.
- `:q` cancel.
- `:wq` save and close.
- `:secret VALUE` store secret in Keychain under current `Secret Ref`.
- `Option+Backspace`: delete previous word in the command line.

Save validation feedback:

- Saving with an empty URL shows red error text in the editor bottom bar.

## History screen

- `j` / `k`: move through runs.
- `r`: replay selected run by current request ID.
- `H` / `L`: nudge vertical divider left/right.
- `{` / `}`: scroll details in the right pane.
- mouse drag on the vertical divider: resize list/detail panes.
- `Esc`: return to main screen.
