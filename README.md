# tuiman

`tuiman` is a lightweight, vim-like terminal API client written in C.

It is intentionally keyboard-only and modeled after terminal workflows used by tools like vim and mutt/neomutt.

## Quickstart

1) Download the right release archive from GitHub Releases:

- Apple Silicon: `tuiman-<tag>-darwin-arm64.tar.gz`
- Intel Mac: `tuiman-<tag>-darwin-x86_64.tar.gz`

2) Extract and install:

```bash
tar -xzf tuiman-<tag>-darwin-<arch>.tar.gz
cd tuiman-<tag>-darwin-<arch>
./install.sh
```

3) Verify and run:

```bash
tuiman --version
tuiman
```

## Install (GitHub Releases)

Download the matching release archive:

- Apple Silicon: `tuiman-<tag>-darwin-arm64.tar.gz`
- Intel Mac: `tuiman-<tag>-darwin-x86_64.tar.gz`

Extract and install:

```bash
tar -xzf tuiman-<tag>-darwin-<arch>.tar.gz
cd tuiman-<tag>-darwin-<arch>
./install.sh
```

By default this installs to `~/.local/bin/tuiman`.

If needed, add `~/.local/bin` to your shell `PATH`:

```bash
export PATH="$HOME/.local/bin:$PATH"
```

Verify:

```bash
tuiman --version
```

Custom prefix:

```bash
TUIMAN_PREFIX=/usr/local ./install.sh
```

## Development Build

Build from source:

```bash
cmake -S . -B build
cmake --build build
```

Run:

```bash
./build/tuiman
```

CLI flags:

```bash
./build/tuiman --help
./build/tuiman --version
```

## Current Status

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
- History run detail includes stored request snapshot and response body per run.
- HTTP execution with `libcurl`.
- History persistence with `sqlite3`.
- macOS Keychain-backed secrets via `security` CLI.
- Export/import for request configs with secrets excluded.

## Storage Locations

- Requests/config: `~/.config/tuiman/`
- History/state: `~/.local/state/tuiman/history.db`
- Cache: `~/.cache/tuiman/`

## Core Commands

From main `:` command line:

- `:new [METHOD] [URL]`
- `:edit`
- `:history`
- `:export [DIR]`
- `:import [DIR]`
- `:help`
- `:q`

See `docs/` for architecture, keybindings, storage, roadmap, and release-process details.

## Release Automation

- Tag pushes matching `v*` run `.github/workflows/release.yml`.
- The workflow builds release binaries on macOS Intel + Apple Silicon, smoke-tests `--help`/`--version`, then attaches tarballs and SHA256 files to the GitHub Release.
- Release tarballs include `install.sh` and `README.md`.
