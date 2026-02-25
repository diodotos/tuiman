import { getSecret } from "./keychain";
import type { RequestItem } from "../types";

export type HttpResponse = {
  status_code: number;
  duration_ms: number;
  body: string;
  error: string;
};

function isJsonishBody(body: string): boolean {
  const text = body.trimStart();
  return text.startsWith("{") || text.startsWith("[");
}

function hasContentTypeHeader(request: RequestItem): boolean {
  return request.header_key.toLowerCase() === "content-type";
}

function appendQueryParam(url: string, key: string, value: string): string {
  const sep = url.includes("?") ? "&" : "?";
  return `${url}${sep}${encodeURIComponent(key)}=${encodeURIComponent(value)}`;
}

export async function sendRequest(request: RequestItem): Promise<HttpResponse> {
  const started = performance.now();

  let url = request.url;
  const headers = new Headers();
  let error = "";

  if (request.header_key) {
    headers.set(request.header_key, request.header_value);
  }

  if ((request.auth_type === "bearer" || request.auth_type === "jwt") && request.auth_secret_ref) {
    const secret = await getSecret(request.auth_secret_ref);
    if (secret) {
      headers.set("Authorization", `Bearer ${secret}`);
    }
  } else if (request.auth_type === "api_key" && request.auth_secret_ref) {
    const secret = await getSecret(request.auth_secret_ref);
    if (secret) {
      const keyName = request.auth_key_name || "X-API-Key";
      const location = request.auth_location || "header";
      if (location === "query") {
        url = appendQueryParam(url, keyName, secret);
      } else {
        headers.set(keyName, secret);
      }
    }
  } else if (request.auth_type === "basic" && request.auth_secret_ref) {
    const secret = await getSecret(request.auth_secret_ref);
    if (secret != null) {
      const userpass = `${request.auth_username}:${secret}`;
      headers.set("Authorization", `Basic ${Buffer.from(userpass).toString("base64")}`);
    }
  }

  if (request.body && isJsonishBody(request.body) && !hasContentTypeHeader(request)) {
    headers.set("Content-Type", "application/json");
    headers.set("Accept", "application/json");
  }

  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 30_000);

  try {
    const response = await fetch(url, {
      method: request.method,
      headers,
      body: request.body || undefined,
      signal: controller.signal,
    });

    const body = await response.text();
    if (!response.ok) {
      error = `HTTP status ${response.status}`;
    }

    return {
      status_code: response.status,
      duration_ms: Math.round(performance.now() - started),
      body,
      error,
    };
  } catch (err) {
    return {
      status_code: 0,
      duration_ms: Math.round(performance.now() - started),
      body: "",
      error: err instanceof Error ? err.message : String(err),
    };
  } finally {
    clearTimeout(timeout);
  }
}
