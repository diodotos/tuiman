# Storage

`tuiman` uses local user directories:

- Config root: `~/.config/tuiman/`
- Requests dir: `~/.config/tuiman/requests/`
- State root: `~/.local/state/tuiman/`
- History DB: `~/.local/state/tuiman/history.db`
- Cache root: `~/.cache/tuiman/`

## Request file format

Each request is stored as `<request-id>.json` and includes:

- `id`, `name`, `method`, `url`
- `header_key`, `header_value`
- `body`
- `auth_type`, `auth_secret_ref`, `auth_key_name`, `auth_location`, `auth_username`
- `updated_at`

## History schema

SQLite table `runs` stores:

- request identity (`request_id`, `request_name`)
- method/url
- status and duration
- error text
- created timestamp

## Export format

Export writes a directory containing:

- `manifest.json`
- `requests/*.json`

During export, `auth_secret_ref` is scrubbed from exported request files.
