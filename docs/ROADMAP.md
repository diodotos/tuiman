# Roadmap

## v0.1 (current)

- C rewrite with vim-like modal flow.
- Main request list + preview + inline bottom cmd/search line.
- New-request vim-style editor.
- HTTP send via `libcurl`.
- History via `sqlite3`.
- macOS Keychain secret storage.
- Export/import directory bundles with secrets excluded.

## Next

- `?` mode fzf-style fuzzy search experience.
- Dynamic filter-as-you-type behavior; `Enter` locks the filter and returns to normal vim navigation.
- Rich response inspection (headers/body toggles, paging).
- Better header editing and preview display.
- Better query-parameter editing interface.
- Dedicated auth editor screen with stronger validation.

## Later

- Linux and Windows keyring backends.
- Import/export compatibility helpers.
- Optional OpenAPI import.
