# Security

## Secret handling

- Request JSON files never contain secret values.
- Secrets are stored in macOS Keychain.
- Requests keep only a `Secret Ref` string.

## macOS backend

Current implementation uses `/usr/bin/security` with service name `tuiman`:

- Set: `add-generic-password ... -U`
- Get: `find-generic-password ... -w`
- Delete: `delete-generic-password ...`

This is macOS-first and intentionally lightweight.

## Export behavior

- Export excludes secrets.
- Export scrubs `auth_secret_ref` in exported request files.
- Manifest records scrubbed secret ref count.

## Operational notes

- To use auth at runtime, ensure the matching secret exists in Keychain.
- You can store a secret from the new editor using `:secret VALUE`.
