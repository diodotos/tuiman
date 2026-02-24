# tuiman

`tuiman` is a lightweight, vim-like terminal API client written in C.

It is intentionally keyboard-only and modeled after terminal workflows used by tools like vim and mutt/neomutt.

## Current status

This is the fresh C rewrite (macOS-first) with:

- Split-pane TUI (`ncurses`) with vim-like modes.
- Request definitions stored as JSON files.
- Request delete from main list with confirmation.
- Body edits validate JSON and auto-format valid JSON.
- HTTP execution with `libcurl`.
- History persistence with `sqlite3`.
- macOS Keychain-backed secrets via `security` CLI.
- Export/import for request configs with secrets excluded.

## Build

```bash
cmake -S . -B build
cmake --build build
```

Run:

```bash
./build/tuiman
```

## Storage locations

- Requests/config: `~/.config/tuiman/`
- History/state: `~/.local/state/tuiman/history.db`
- Cache: `~/.cache/tuiman/`

## Core commands

From main `:` command line:

- `:new [METHOD] [URL]`
- `:history`
- `:export [DIR]`
- `:import [DIR]`
- `:help`
- `:q`

See the `docs/` folder for architecture, keybindings, storage, and roadmap details.
