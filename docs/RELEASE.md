# Release Process (rewrite branch)

This rewrite now packages a TypeScript OpenTUI app (no Rust sidecar).

## Current state

- Release workflow is in `.github/workflows/release.yml`.
- Assets include:
  - launcher script (`tuiman`)
  - `frontend/` app directory
  - `install.sh`
  - `README.md`

## Rewrite release prerequisites

- Frontend dependencies install (`bun install --cwd frontend`).
- Frontend typecheck passes.
- Launcher flags work (`--help`, `--version`).
- PTY parity checklist pass for minimum baseline behavior.

## Release shape

1. Install frontend deps:

   ```bash
   bun install --cwd frontend
   ```

2. Run smoke checks:

   ```bash
   ./scripts/tuiman --version
   ./scripts/tuiman --help
   bun run --cwd frontend typecheck
   ./tools/parity/run-smoke.sh
   ```

3. Package `tuiman`, `frontend/`, `install.sh`, and `README.md` into tarball.

4. Publish tag-driven GitHub release.

## TODO

- Add a compiled launcher option so end users do not need Bun preinstalled.
