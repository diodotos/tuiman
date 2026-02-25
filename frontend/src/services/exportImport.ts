import { mkdirSync, readdirSync, readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";

import { saveRequest, listRequests } from "./requestStore";
import type { RequestItem } from "../types";

type ExportManifest = {
  exported_at: string;
  request_count: number;
  scrubbed_secret_ref_count: number;
};

function nowStamp(): string {
  const d = new Date();
  const pad = (n: number) => String(n).padStart(2, "0");
  return `${d.getFullYear()}${pad(d.getMonth() + 1)}${pad(d.getDate())}-${pad(d.getHours())}${pad(d.getMinutes())}${pad(d.getSeconds())}`;
}

function normalizeImportedRequest(value: Partial<RequestItem>): RequestItem {
  return {
    id: value.id || "",
    name: value.name || "New Request",
    method: value.method || "GET",
    url: value.url || "",
    header_key: value.header_key || "",
    header_value: value.header_value || "",
    body: value.body || "",
    auth_type: value.auth_type || "none",
    auth_secret_ref: value.auth_secret_ref || "",
    auth_key_name: value.auth_key_name || "",
    auth_location: value.auth_location || "",
    auth_username: value.auth_username || "",
    updated_at: value.updated_at || "",
  };
}

export async function exportRequests(dirArg?: string): Promise<{ directory: string; count: number; scrubbed: number }> {
  const requests = await listRequests();
  const targetDir = dirArg && dirArg.trim().length > 0 ? dirArg : `./tuiman-export-${nowStamp()}`;
  const requestDir = join(targetDir, "requests");
  mkdirSync(requestDir, { recursive: true });

  let scrubbed = 0;
  for (const request of requests) {
    const copy = { ...request };
    if (copy.auth_secret_ref) {
      scrubbed += 1;
      copy.auth_secret_ref = "";
    }
    writeFileSync(join(requestDir, `${copy.id}.json`), `${JSON.stringify(copy, null, 2)}\n`, "utf8");
  }

  const manifest: ExportManifest = {
    exported_at: new Date().toISOString(),
    request_count: requests.length,
    scrubbed_secret_ref_count: scrubbed,
  };
  writeFileSync(join(targetDir, "manifest.json"), `${JSON.stringify(manifest, null, 2)}\n`, "utf8");

  return {
    directory: targetDir,
    count: requests.length,
    scrubbed,
  };
}

export async function importRequests(dir: string): Promise<{ imported: number }> {
  const requestDir = join(dir, "requests");
  const entries = readdirSync(requestDir, { withFileTypes: true });
  let imported = 0;

  for (const entry of entries) {
    if (!entry.isFile() || !entry.name.endsWith(".json")) {
      continue;
    }
    const path = join(requestDir, entry.name);
    const raw = readFileSync(path, "utf8");
    const value = JSON.parse(raw) as Partial<RequestItem>;
    const normalized = normalizeImportedRequest(value);
    await saveRequest(normalized);
    imported += 1;
  }

  return { imported };
}
