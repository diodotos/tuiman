# Roadmap

## v0.2 rewrite branch (current)

- C code removed from `tui-man-rust` in favor of Rust + OpenTUI scaffold.
- Rust backend workspace + stdio RPC baseline in place.
- OpenTUI React shell with split-pane visual structure in place.
- PTY parity checklist established.

## Next (parity milestones)

- Complete backend request store parity (read/write/list/delete + defaults).
- Complete backend history parity (schema/migrations/run snapshots/response body).
- Implement backend HTTP parity for auth/header/body behavior.
- Implement full modal frontend parity (`NORMAL`, `ACTION`, `SEARCH`, `COMMAND`, editor/history/help screens).
- Implement divider drag/resize and body scrolling parity.

## Cleanup and UX refinement

- Improve visual consistency while retaining minimalism.
- Tighten layout/wrapping behavior on narrow terminals.
- Add fuzzy search mode and richer response inspection.

## Later

- Linux and Windows keyring support in backend.
- OpenAPI import.
- Release packaging parity for dual binaries (`tuiman`, `tuiman-backend`).
