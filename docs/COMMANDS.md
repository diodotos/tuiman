# Commands

## Backend executable flags

`backend/apps/tuiman-backend` currently supports:

- `--help` / `-h`
- `--version` / `-v`

## Backend stdio RPC methods

Current scaffold methods:

- `ping`
  - Returns backend version.
- `bootstrap`
  - Returns request list and recent runs.

## Target UI command parity (planned)

Main command line is opened with `:` from the main screen.

- `:new [METHOD] [URL]`
- `:edit`
- `:history`
- `:export [DIR]`
- `:import [DIR]`
- `:help`
- `:q`

Editor command mode targets:

- `:w`
- `:q`
- `:wq`
- `:secret VALUE`
