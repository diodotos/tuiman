# Roadmap

## v0.2 rewrite branch (current)

- C code removed from `tui-man-rust` in favor of TypeScript + OpenTUI rewrite.
- Single-process service architecture in place (request store, history store, HTTP client, keychain).
- OpenTUI multi-screen shell (main/editor/history/help) in place.
- Import/export command path is wired.
- External body editor path is wired for main action row and editor.
- JSON validate + pretty-format on save is wired for JSON-like request bodies.
- Reverse search trigger (`?`) is wired.
- Split-pane mouse drag behavior tightened with divider snap-start handling.
- PTY parity checklist established.

## Next (parity milestones)

- Add true reverse/fuzzy search behavior beyond the shared substring filter engine.

## Cleanup and UX refinement

- Improve visual consistency while retaining minimalism.
- Tighten layout/wrapping behavior on narrow terminals.
- Add fuzzy search mode and richer response inspection.

## Later

- Linux and Windows keyring support.
- OpenAPI import.
- Compiled launcher release option.
