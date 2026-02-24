# Architecture

## Goals

- Keep the interface modal and vim-like.
- Keep the project lightweight and local-first.
- Keep request files human-readable.
- Keep secrets out of request files and exports.

## Stack

- TUI: `ncurses`
- HTTP: `libcurl`
- History: `sqlite3`
- Request storage: JSON files
- Secret storage (macOS): Keychain via `/usr/bin/security`

## Core modules

- `src/main.c`
  - Event loop, mode/screen state machine, rendering.
  - Main screen, new-request editor screen, history screen, help screen.
  - Body-edit JSON validation/formatting integration.
  - Main split view uses isolated ncurses windows per pane.
  - Main view has a dedicated bottom response pane with metadata + body preview.
  - Preview/response text uses internal wrapping logic (not terminal auto-wrap).
- `src/core/json_body_macos.m`
  - macOS JSON parse + pretty formatting using Foundation.
- `src/store/request_store.c`
  - Request JSON read/write/list/delete.
- `src/store/history_store.c`
  - Run history schema and queries.
- `src/net/http_client.c`
  - Request execution and auth/header application.
- `src/auth/keychain_macos.c`
  - Secret set/get/delete using Keychain CLI integration.
- `src/store/export_import.c`
  - Export/import directories with secret refs scrubbed.

## Mode model

Main screen modes:

- `NORMAL`
- `ACTION`
- `DELETE_CONFIRM`
- `SEARCH` (`/`)
- `REVERSE` (`?`, currently same substring engine)
- `COMMAND` (`:`)

New request editor modes:

- `NEW_NORMAL`
- `NEW_INSERT`
- `NEW_COMMAND`

## Screen model

- Main split view: request list + request preview (top), response pane (bottom), status/cmdline (last row).
- Main split view reflows by terminal width and can temporarily hide request preview when too narrow.
- New request editor: field list + preview + vim-like bottom command line.
- History: run list + run details.
- Help: key/command quick reference.
