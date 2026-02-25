# Release Process

This project uses a tag-driven release workflow.

## Important: regular pushes vs releases

- Regular pushes to `main` do **not** create a GitHub Release.
- The release workflow runs when you push a tag matching `v*` (for example `v0.1.0`).
- You can also run it manually from GitHub Actions (`workflow_dispatch`) for testing.

So yes: releases are a distinct process.

## Prerequisites

- Changes merged into `main`.
- `CMakeLists.txt` project version matches intended release version.
- Working tree clean locally.

## Create a release

1. Pull latest `main`:

   ```bash
   git checkout main
   git pull --ff-only
   ```

2. Ensure version is correct in `CMakeLists.txt` (`project(tuiman VERSION x.y.z ...)`).

3. Create and push tag:

   ```bash
   git tag v0.1.0
   git push origin v0.1.0
   ```

4. Watch GitHub Actions: `Release Build` workflow.

5. After success, verify release assets exist on GitHub Release page:
   - `tuiman-<tag>-darwin-x86_64.tar.gz`
   - `tuiman-<tag>-darwin-arm64.tar.gz`
   - matching `.sha256` files

## What the workflow does

- Builds on `macos-13` and `macos-14`.
- Runs smoke tests:
  - `./build/tuiman --version`
  - `./build/tuiman --help`
- Packages tarballs and SHA256 checksums.
- Publishes/updates GitHub Release for the tag and uploads assets.

## Troubleshooting

- If workflow fails, fix on `main`, then delete/recreate the tag and push again.
- If you only need to test workflow logic without a public release, use manual run (`workflow_dispatch`).
