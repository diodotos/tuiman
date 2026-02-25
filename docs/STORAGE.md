# Storage

The rewrite keeps the same local paths to preserve compatibility.

- Config root: `~/.config/tuiman/`
- Requests dir: `~/.config/tuiman/requests/`
- State root: `~/.local/state/tuiman/`
- History DB: `~/.local/state/tuiman/history.db`
- Cache root: `~/.cache/tuiman/`

## Request file format (compatibility target)

Each request remains `<request-id>.json` with fields:

- `id`, `name`, `method`, `url`
- `header_key`, `header_value`
- `body`
- `auth_type`, `auth_secret_ref`, `auth_key_name`, `auth_location`, `auth_username`
- `updated_at`

## History storage

Rewrite target keeps SQLite `runs` compatibility:

- request identity (`request_id`, `request_name`)
- method/url
- status/duration
- error text
- created timestamp
- request snapshot and response body

## Current scaffold state

- Request loading from request JSON files is wired.
- History loading is stubbed pending SQLite migration in Rust.
