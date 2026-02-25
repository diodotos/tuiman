# Roadmap

## v0.2 rewrite branch (current)

- C code removed from `tui-man-rust` in favor of TypeScript + OpenTUI rewrite.
- Single-process service architecture in place (request store, history store, HTTP client, keychain).
- OpenTUI multi-screen shell (main/editor/history/help) in place.
- PTY parity checklist established.

## Next (parity milestones)

- Complete import/export parity (`:import`, `:export`).
- Wire external body editor flow in main action row and editor screen.
- Add JSON validate + pretty-format on save parity.
- Tighten mouse drag behavior parity for all split panes.
- Add reverse/fuzzy search behavior parity (`?`, `n`, `N`).

## Cleanup and UX refinement

- Improve visual consistency while retaining minimalism.
- Tighten layout/wrapping behavior on narrow terminals.
- Add fuzzy search mode and richer response inspection.

## Later

- Linux and Windows keyring support.
- OpenAPI import.
- Compiled launcher release option.
