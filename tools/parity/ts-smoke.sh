#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TMP_HOME="$(mktemp -d)"

cleanup() {
  rm -rf "${TMP_HOME}"
}
trap cleanup EXIT

echo "[ts-smoke] launcher flags"
HOME="${TMP_HOME}" "${ROOT_DIR}/scripts/tuiman" --version
HOME="${TMP_HOME}" "${ROOT_DIR}/scripts/tuiman" --help >/tmp/tuiman-ts-help.txt
grep -q "Usage:" /tmp/tuiman-ts-help.txt

echo "[ts-smoke] service roundtrip"
HOME="${TMP_HOME}" bun --cwd "${ROOT_DIR}/frontend" -e '
  import { mkdtempSync } from "node:fs";
  import { tmpdir } from "node:os";
  import { join } from "node:path";
  import { newRequest } from "./src/types";
  import {
    saveRequest,
    listRequests,
    recordRun,
    listRuns,
    sendRequest,
    deleteRequest,
    exportRequests,
    importRequests,
  } from "./src/services/api";

  const req = await saveRequest(newRequest("GET", "https://example.com"));
  if (!req.id) throw new Error("saveRequest did not assign id");

  const all = await listRequests();
  if (!all.some((item) => item.id === req.id)) throw new Error("saved request missing from listRequests");

  const sent = await sendRequest(req);
  if (typeof sent.status_code !== "number") throw new Error("sendRequest missing status_code");

  await recordRun({
    id: 0,
    request_id: req.id,
    request_name: req.name,
    method: req.method,
    url: req.url,
    status_code: sent.status_code,
    duration_ms: sent.duration_ms,
    error: sent.error,
    created_at: new Date().toISOString(),
    request_snapshot: "snapshot",
    response_body: sent.body,
  });

  const runs = await listRuns(20);
  if (!runs.some((run) => run.request_id === req.id)) throw new Error("recorded run missing from listRuns");

  const exportDir = mkdtempSync(join(tmpdir(), "tuiman-export-smoke-"));
  const exported = await exportRequests(exportDir);
  if (exported.count < 1) throw new Error("exportRequests exported zero requests");

  await deleteRequest(req.id);

  const imported = await importRequests(exportDir);
  if (imported.imported < 1) throw new Error("importRequests imported zero requests");
'

echo "[ts-smoke] done"
