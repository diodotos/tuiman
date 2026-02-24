# Keybindings

## Main screen

- `j` / `k`: move selection.
- `gg` / `G`: jump top/bottom.
- `Enter`: open action row for selected request.
- `d`: delete selected request with confirmation prompt.
- `Esc`: clear filter or clear transient bottom status back to idle hint.

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

Delete confirmation prompt:

- `y`: confirm delete.
- `n` or `Esc`: cancel delete.

## New request editor

Normal mode:

- `j` / `k`: move between fields.
- `h` / `l` on Method: cycle HTTP method.
- `i` or `Enter`: edit selected field (insert mode).
- `e`: edit body in external editor.
- `:`: editor command line.
- `Ctrl+s`: save request.
- `Esc`: cancel editor.

Insert mode:

- Type to edit current field.
- `Backspace`: delete character.
- `Enter`: apply and return to normal mode.
- `Esc`: return to normal mode.

Editor command mode (`:`):

- `:w` save.
- `:q` cancel.
- `:wq` save and close.
- `:secret VALUE` store secret in Keychain under current `Secret Ref`.

## History screen

- `j` / `k`: move through runs.
- `r`: replay selected run by current request ID.
- `Esc`: return to main screen.
