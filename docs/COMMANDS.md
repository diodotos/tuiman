# Commands

Main command line is opened with `:` from the main screen.

- `:new [METHOD] [URL]`
  - Opens the vim-like new request editor.
  - Prefills method and URL when provided.

- `:history`
  - Opens run history screen.

- `:export [DIR]`
  - Exports request definitions to an export directory.
  - Default path: `./tuiman-export-YYYYmmdd-HHMMSS`.
  - Secrets are excluded.

- `:import [DIR]`
  - Imports request definitions from an export directory.

- `:help`
  - Opens help screen.

- `:q`
  - Quits `tuiman`.

## New editor commands

In new-request editor command mode (`:`):

- `:w` save request.
- `:q` cancel editor.
- `:wq` save and close.
- `:secret VALUE` save secret in Keychain using the current `Secret Ref` field.
