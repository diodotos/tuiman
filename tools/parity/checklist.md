# C vs Rust/OpenTUI Parity Checklist

Use this checklist during PTY-based validation loops.

- Startup split-pane structure appears (left requests + right preview + bottom response).
- Default status line contains vim/mutt-style navigation hints.
- Request list keyboard navigation works (`j/k`, arrows, `gg/G`).
- Main command line behavior works (`:help`, `:new`, `:edit`, `:history`, `:q`).
- Action row behavior works (`Enter` then `y/e/a`, cancel with `Esc`/`n`).
- Delete confirmation flow works (`d`, confirm/cancel).
- Editor behavior works (`i`, `Esc`, `Ctrl+s`, `:w`, `:q`, `:wq`).
- Method cycle behavior in editor works (`h/l`).
- JSON body validate + pretty-format on save works.
- Request preview and response body scrolling work (`{ }` and `[ ]`).
- History split view and replay behavior work.
- Mouse divider drag works in main/editor/history split panes.
- Release flags remain compatible (`--help`, `--version`).
