# tuiman (Rust + OpenTUI rewrite)

`tuiman` is being rewritten as:

- Rust backend (`backend/`) for data, HTTP, persistence, and platform services.
- OpenTUI React frontend (`frontend/`) for terminal UX.

The goal remains a vim/mutt-style terminal API client with parity to the stable C line, while improving architecture and visuals.

## Current Rewrite Status

This branch now contains the rewrite scaffold and intentionally removes residual C implementation from `tui-man-rust`.

- C source and CMake build files removed.
- Rust backend workspace scaffolded with typed crate boundaries.
- OpenTUI React frontend scaffolded with split-pane visual skeleton and modal keybinding foundation.
- PTY parity checklist added at `tools/parity/checklist.md`.

Implemented now:

- Backend CLI flags: `--help`, `--version`.
- Backend stdio JSON-RPC baseline methods: `ping`, `bootstrap`.
- Frontend bootstraps request list from backend and renders request/preview/response panes.

Not yet migrated:

- Full send/edit/history/auth parity.
- release packaging for Rust+OpenTUI binaries.

## Repository Layout

```text
backend/
  Cargo.toml                 # workspace
  apps/tuiman-backend/       # stdio RPC backend executable
  crates/
    tuiman-domain/           # shared model types
    tuiman-storage/          # request/history persistence adapters
    tuiman-http/             # HTTP execution adapter
    tuiman-keychain/         # macOS keychain integration
    tuiman-ipc/              # frontend/backend RPC contracts

frontend/
  package.json               # Bun + OpenTUI React app
  tsconfig.json
  src/
    index.tsx
    App.tsx
    rpc/client.ts

tools/parity/
  checklist.md               # C vs rewrite runtime parity checks
```

## Local Development

### 1) Build backend

```bash
cd backend
cargo build -p tuiman-backend
```

### 2) Run frontend

```bash
cd frontend
bun install
bun run src/index.tsx
```

If you built the backend in debug mode, frontend uses:

- `../backend/target/debug/tuiman-backend`

Override path when needed:

```bash
TUIMAN_BACKEND_PATH=/absolute/path/to/tuiman-backend bun run src/index.tsx
```

## Parity Validation Workflow

Use PTY sessions to validate behavior against the C baseline worktree.

- C baseline binary: `../tui-man/build/tuiman`
- Rewrite runtime: `frontend/src/index.tsx` + Rust backend
- Checklist: `tools/parity/checklist.md`

## Notes

- OpenTUI uses Bun and may require Zig for native build paths.
- This branch preserves the stable C line in the separate `tui-man` worktree.
