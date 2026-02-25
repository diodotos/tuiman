# Install

## From GitHub release assets (recommended for now)

1. Download the matching archive for your machine from GitHub Releases:
   - Apple Silicon: `*-darwin-arm64.tar.gz`
   - Intel Mac: `*-darwin-x86_64.tar.gz`

2. Extract it:

   ```bash
   tar -xzf tuiman-vX.Y.Z-darwin-<arch>.tar.gz
   cd tuiman-vX.Y.Z-darwin-<arch>
   ```

3. Install to a `PATH` directory:

   ```bash
   ./install.sh
   ```

   By default this installs to `~/.local/bin/tuiman`.

4. If needed, add `~/.local/bin` to your shell `PATH`:

   ```bash
   export PATH="$HOME/.local/bin:$PATH"
   ```

5. Verify:

   ```bash
   tuiman --version
   ```

### Install to a custom prefix

```bash
TUIMAN_PREFIX=/usr/local ./install.sh
```

## From source

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build --prefix "$HOME/.local"
```

Then ensure `~/.local/bin` is on `PATH`.
