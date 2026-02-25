# tuiman

`tuiman` is a lightweight, vim-like terminal API client written in C.

It is intentionally keyboard-only and modeled after terminal workflows used by tools like vim and mutt/neomutt.

## Current status

This is the fresh C rewrite (macOS-first) with:

- Split-pane TUI (`ncurses`) with vim-like modes.
- Main top split (request list + request preview) plus bottom response pane.
- Mouse-draggable pane dividers (tmux-style) and keyboard resize nudges.
- Request definitions stored as JSON files.
- Request delete from main list with confirmation.
- Full edit flow for existing requests (`E` key or `:edit`).
- Editor pane uses the same styled split layout as main and supports mouse divider drag.
- Method in editor is cycle-only (`h`/`l`) to avoid accidental free-text methods.
- Preview bodies (request/response/editor/history details) are wrapped and scrollable.
- Response preview stores the full response body in memory for scrolling.
- Body edits validate JSON and auto-format valid JSON.
- History screen uses the same modernized split-pane style as main/editor.
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
- `:edit`
- `:history`
- `:export [DIR]`
- `:import [DIR]`
- `:help`
- `:q`

See the `docs/` folder for architecture, keybindings, storage, and roadmap details.
