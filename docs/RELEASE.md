# Release Process (rewrite branch)

This document is now in transition for Rust + OpenTUI packaging.

## Current state

- Legacy C release workflow file still exists at `.github/workflows/release.yml` and is obsolete for this branch.
- Rewrite release automation must package at least:
  - frontend launch binary/script (`tuiman`)
  - backend executable (`tuiman-backend`)
  - `install.sh` and `README.md`

## Rewrite release prerequisites

- `backend` workspace version and `frontend` package version aligned.
- Backend compiles in release mode.
- Frontend runtime bootstraps and can reach backend.
- PTY parity checklist pass for minimum baseline behavior.

## Planned release shape

1. Build backend:

   ```bash
   cd backend
   cargo build --release -p tuiman-backend
   ```

2. Build frontend runtime entrypoint (strategy pending: Bun script vs compiled launcher).

3. Package assets into architecture-specific tarballs.

4. Smoke tests:

   - `tuiman --version`
   - `tuiman-backend --version`
   - frontend boot + backend bootstrap over RPC

5. Publish tag-driven GitHub release.

## TODO

- Replace `.github/workflows/release.yml` with Rust/OpenTUI aware pipeline.
- Update `scripts/install.sh` for dual-binary install.
