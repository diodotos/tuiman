import { readdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { join } from "node:path";

import { ensurePaths } from "./paths";
import type { RequestItem } from "../types";

function nowIsoNoMillis(): string {
  return new Date().toISOString().replace(/\.\d{3}Z$/, "Z");
}

function asString(value: unknown, fallback: string): string {
  return typeof value === "string" ? value : fallback;
}

function parseRequest(raw: string): RequestItem {
  const value = JSON.parse(raw) as Record<string, unknown>;
  return {
    id: asString(value.id, crypto.randomUUID()),
    name: asString(value.name, "New Request"),
    method: asString(value.method, "GET"),
    url: asString(value.url, ""),
    header_key: asString(value.header_key, ""),
    header_value: asString(value.header_value, ""),
    body: asString(value.body, ""),
    auth_type: asString(value.auth_type, "none"),
    auth_secret_ref: asString(value.auth_secret_ref, ""),
    auth_key_name: asString(value.auth_key_name, ""),
    auth_location: asString(value.auth_location, ""),
    auth_username: asString(value.auth_username, ""),
    updated_at: asString(value.updated_at, nowIsoNoMillis()),
  };
}

export async function listRequests(): Promise<RequestItem[]> {
  const paths = ensurePaths();
  const entries = readdirSync(paths.requestsDir, { withFileTypes: true });
  const requests: RequestItem[] = [];

  for (const entry of entries) {
    if (!entry.isFile() || !entry.name.endsWith(".json")) {
      continue;
    }

    const filePath = join(paths.requestsDir, entry.name);
    try {
      const raw = readFileSync(filePath, "utf8");
      requests.push(parseRequest(raw));
    } catch {
      continue;
    }
  }

  requests.sort((a, b) => a.name.toLowerCase().localeCompare(b.name.toLowerCase()));
  return requests;
}

export async function saveRequest(input: RequestItem): Promise<RequestItem> {
  const paths = ensurePaths();
  const request: RequestItem = {
    ...input,
    id: input.id || crypto.randomUUID(),
    name: input.name || "New Request",
    method: input.method || "GET",
    auth_type: input.auth_type || "none",
    updated_at: nowIsoNoMillis(),
  };

  const path = join(paths.requestsDir, `${request.id}.json`);
  writeFileSync(path, `${JSON.stringify(request, null, 2)}\n`, "utf8");
  return request;
}

export async function deleteRequest(requestId: string): Promise<void> {
  const paths = ensurePaths();
  const path = join(paths.requestsDir, `${requestId}.json`);
  rmSync(path, { force: true });
}
