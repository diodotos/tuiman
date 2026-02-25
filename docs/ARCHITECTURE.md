# Architecture

## Goals

- Keep a modal, keyboard-first vim/mutt workflow.
- Preserve behavior parity with the C line while improving structure.
- Separate UI and execution concerns cleanly.
- Keep local-first storage and secret hygiene.

## Stack

- Frontend: OpenTUI React (`@opentui/react`, Bun runtime).
- Services: TypeScript modules running in the same process.
- Persistence: Bun file APIs + `bun:sqlite`.

## Topology

- One process owns both rendering and data operations.
- `App.tsx` drives screens/modes and calls service functions directly.
- Service modules isolate concerns (request store, history store, keychain, HTTP, paths).

## Service Modules

- `frontend/src/services/paths.ts`
  - canonical local directories (`~/.config/tuiman`, `~/.local/state/tuiman`, `~/.cache/tuiman`).
- `frontend/src/services/requestStore.ts`
  - request JSON load/save/delete and default field handling.
- `frontend/src/services/historyStore.ts`
  - SQLite run history schema, migration, load, and insert.
- `frontend/src/services/httpClient.ts`
  - request execution with auth/header/body behavior and response timing.
- `frontend/src/services/keychain.ts`
  - macOS Keychain wrapper via `/usr/bin/security`.
- `frontend/src/services/exportImport.ts`
  - export bundle generation and import upsert flow.
- `frontend/src/services/api.ts`
  - service facade consumed by UI.

## Frontend Modules

- `frontend/src/index.tsx`
  - OpenTUI renderer bootstrap.
- `frontend/src/App.tsx`
  - main screen + history/editor/help screens and modal state handling.
- `frontend/src/theme.ts`
  - color direction and method color mapping.

## Parity Process

- C baseline runs from sibling worktree (`../tui-man`).
- Rewrite runs as a single TypeScript OpenTUI app.
- Runtime checks are tracked in `tools/parity/checklist.md` and validated via PTY loops.
- `tools/parity/run-smoke.sh` and `tools/parity/ts-smoke.sh` provide repeatable smoke checks.
