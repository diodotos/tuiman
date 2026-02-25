import { listRequests as listRequestsImpl, saveRequest as saveRequestImpl, deleteRequest as deleteRequestImpl } from "./requestStore";
import { listRuns as listRunsImpl, recordRun as recordRunImpl } from "./historyStore";
import { sendRequest as sendRequestImpl, type HttpResponse } from "./httpClient";
import { setSecret } from "./keychain";
import { exportRequests as exportRequestsImpl, importRequests as importRequestsImpl } from "./exportImport";
import type { BootstrapPayload, RequestItem, RunEntry } from "../types";

export { type HttpResponse };

export async function bootstrap(): Promise<BootstrapPayload> {
  const [requests, runs] = await Promise.all([listRequestsImpl(), listRunsImpl(200)]);
  return { requests, runs };
}

export async function listRequests(): Promise<RequestItem[]> {
  return listRequestsImpl();
}

export async function saveRequest(request: RequestItem): Promise<RequestItem> {
  return saveRequestImpl(request);
}

export async function deleteRequest(requestId: string): Promise<void> {
  return deleteRequestImpl(requestId);
}

export async function listRuns(limit = 200): Promise<RunEntry[]> {
  return listRunsImpl(limit);
}

export async function recordRun(run: RunEntry): Promise<void> {
  return recordRunImpl(run);
}

export async function sendRequest(request: RequestItem): Promise<HttpResponse> {
  return sendRequestImpl(request);
}

export async function saveSecret(secretRef: string, value: string): Promise<void> {
  await setSecret(secretRef, value);
}

export async function exportRequests(dir?: string): Promise<{ directory: string; count: number; scrubbed: number }> {
  return exportRequestsImpl(dir);
}

export async function importRequests(dir: string): Promise<{ imported: number }> {
  return importRequestsImpl(dir);
}
