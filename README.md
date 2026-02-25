# tuiman (TypeScript + OpenTUI rewrite)

`tuiman` is now being rewritten as a single-process TypeScript app:

- OpenTUI React frontend for terminal UX.
- Bun-native services for request storage, history (SQLite), HTTP sending, and Keychain integration.

The goal stays the same: keep vim/mutt-style terminal minimalism with near-parity to the stable C line.

## Current Rewrite Status

- Residual C implementation has been removed from this rewrite tree.
- Rust/IPC backend path has been dropped in favor of in-process TypeScript services.
- Main screen, command/search/action/delete flows, editor screen, history screen, and help screen are wired.
- Request and run persistence are local-first and compatible with the C-line data locations.
- `:import` / `:export`, external body editor (`e`), and JSON validate+pretty-on-save are wired.
- Split-pane mouse drag is wired across main/history/editor, with divider snap-start support.

## Repository Layout

```text
frontend/
  package.json
  tsconfig.json
  src/
    index.tsx
    App.tsx
    services/
      api.ts
      requestStore.ts
      historyStore.ts
      httpClient.ts
      keychain.ts
      paths.ts

scripts/
  tuiman
  install.sh

tools/parity/
  checklist.md
  run-smoke.sh
  ts-smoke.sh
```

## Local Development

```bash
cd frontend
bun install
bun run src/index.tsx
```

Or from repo root:

```bash
./scripts/tuiman
```

CLI flags:

```bash
./scripts/tuiman --version
./scripts/tuiman --help
```

## Parity Validation Workflow

Use PTY sessions to validate behavior against the C baseline worktree.

- C baseline binary: `../tui-man/build/tuiman`
- Rewrite runtime: `./scripts/tuiman`
- Checklist: `tools/parity/checklist.md`
- Smoke runner: `tools/parity/run-smoke.sh`

## Notes

- OpenTUI uses Bun and may require Zig for native build paths.
- This branch preserves the stable C line in the separate `tui-man` worktree.
