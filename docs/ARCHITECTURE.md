# Architecture

## Goals

- Keep a modal, keyboard-first vim/mutt workflow.
- Preserve behavior parity with the C line while improving structure.
- Separate UI and execution concerns cleanly.
- Keep local-first storage and secret hygiene.

## Stack

- Frontend: OpenTUI React (`@opentui/react`, Bun runtime).
- Backend: Rust workspace (`cargo`, typed crate boundaries).
- Frontend/backend protocol: stdio JSON-RPC.

## Topology

- Frontend process renders terminal UI and handles key/mouse interactions.
- Backend process owns request storage, history, auth/keychain, and HTTP execution.
- Frontend sends RPC envelopes over stdin; backend replies on stdout with JSON lines.

## Rust Workspace

- `backend/apps/tuiman-backend`
  - backend executable and RPC dispatch loop.
- `backend/crates/tuiman-domain`
  - shared request/run/path models.
- `backend/crates/tuiman-ipc`
  - RPC envelope and response contracts.
- `backend/crates/tuiman-storage`
  - request and history persistence adapters.
- `backend/crates/tuiman-http`
  - HTTP send adapter (migration target for C `libcurl` behavior).
- `backend/crates/tuiman-keychain`
  - macOS keychain integration (`/usr/bin/security`).

## Frontend Modules

- `frontend/src/index.tsx`
  - OpenTUI renderer bootstrap.
- `frontend/src/App.tsx`
  - primary split-pane UI skeleton and keyboard-mode baseline.
- `frontend/src/rpc/client.ts`
  - backend process invocation + JSON request/response plumbing.
- `frontend/src/theme.ts`
  - color direction and method color mapping.

## Parity Process

- C baseline runs from sibling worktree (`../tui-man`).
- Rewrite runs with OpenTUI + Rust backend.
- Runtime checks are tracked in `tools/parity/checklist.md` and validated via PTY loops.
