import { Database } from "bun:sqlite";

import { ensurePaths } from "./paths";
import type { RunEntry } from "../types";

const HISTORY_SCHEMA_SQL = `
CREATE TABLE IF NOT EXISTS runs (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  request_id TEXT NOT NULL,
  request_name TEXT NOT NULL,
  method TEXT NOT NULL,
  url TEXT NOT NULL,
  status_code INTEGER,
  duration_ms INTEGER,
  error TEXT,
  created_at TEXT NOT NULL,
  request_snapshot TEXT,
  response_body TEXT
);
`;

function nowIsoNoMillis(): string {
  return new Date().toISOString().replace(/\.\d{3}Z$/, "Z");
}

function openDb(): Database {
  const paths = ensurePaths();
  const db = new Database(paths.historyDbPath, { create: true });
  db.exec(HISTORY_SCHEMA_SQL);

  try {
    db.exec("ALTER TABLE runs ADD COLUMN request_snapshot TEXT;");
  } catch {}
  try {
    db.exec("ALTER TABLE runs ADD COLUMN response_body TEXT;");
  } catch {}

  return db;
}

export async function listRuns(limit = 200): Promise<RunEntry[]> {
  const db = openDb();
  try {
    const stmt = db.query<{
      id: number;
      request_id: string;
      request_name: string;
      method: string;
      url: string;
      status_code: number | null;
      duration_ms: number | null;
      error: string | null;
      created_at: string;
      request_snapshot: string | null;
      response_body: string | null;
    }, [number]>(
      `SELECT id, request_id, request_name, method, url, status_code, duration_ms, error, created_at, request_snapshot, response_body
       FROM runs
       ORDER BY id DESC
       LIMIT ?1`,
    );
    const rows = stmt.all(limit);

    return rows.map((row) => ({
      id: row.id,
      request_id: row.request_id,
      request_name: row.request_name,
      method: row.method,
      url: row.url,
      status_code: row.status_code ?? 0,
      duration_ms: row.duration_ms ?? 0,
      error: row.error ?? "",
      created_at: row.created_at,
      request_snapshot: row.request_snapshot ?? "",
      response_body: row.response_body ?? "",
    }));
  } finally {
    db.close();
  }
}

export async function recordRun(input: RunEntry): Promise<void> {
  const db = openDb();
  try {
    const stmt = db.query(
      `INSERT INTO runs
       (request_id, request_name, method, url, status_code, duration_ms, error, created_at, request_snapshot, response_body)
       VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10)`,
    );

    stmt.run(
      input.request_id,
      input.request_name,
      input.method,
      input.url,
      input.status_code,
      input.duration_ms,
      input.error,
      input.created_at || nowIsoNoMillis(),
      input.request_snapshot,
      input.response_body,
    );
  } finally {
    db.close();
  }
}
