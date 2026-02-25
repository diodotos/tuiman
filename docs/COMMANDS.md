# Commands

## Launcher flags

`scripts/tuiman` supports:

- `--help` / `-h`
- `--version` / `-v`

## Main command mode

Main command line opens with `:` from main screen.

- `:new [METHOD] [URL]`
  - Implemented (opens editor with optional prefilled method + URL).
- `:edit`
  - Implemented (opens editor for selected request).
- `:history`
  - Implemented (opens history screen).
- `:export [DIR]`
  - Placeholder status message.
- `:import [DIR]`
  - Placeholder status message.
- `:help`
  - Implemented (opens help screen).
- `:q`
  - Implemented (quit).

## Editor command mode

- `:w` save and return to main.
- `:q` cancel and return to main.
- `:wq` save and return to main.
- `:secret VALUE` save value to Keychain using current `Secret Ref`.
