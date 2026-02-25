import type { BootstrapPayload } from "../types";

type RpcResponse = {
  id: number;
  ok: boolean;
  result?: unknown;
  error?: string;
};

async function sendBackendMessage(method: string, params: unknown): Promise<RpcResponse> {
  const backendPath = process.env.TUIMAN_BACKEND_PATH ?? "../backend/target/debug/tuiman-backend";
  const payload = JSON.stringify({ id: Date.now(), method, params });

  const proc = Bun.spawn([backendPath], {
    stdin: "pipe",
    stdout: "pipe",
    stderr: "pipe",
  });

  proc.stdin.write(`${payload}\n`);
  proc.stdin.end();

  const text = await new Response(proc.stdout).text();
  await proc.exited;

  const firstLine = text.split("\n").find((line) => line.trim().length > 0);
  if (!firstLine) {
    return { id: 0, ok: false, error: "backend returned no response" };
  }

  return JSON.parse(firstLine) as RpcResponse;
}

export async function bootstrap(): Promise<BootstrapPayload> {
  const response = await sendBackendMessage("bootstrap", {});
  if (!response.ok) {
    throw new Error(response.error ?? "bootstrap failed");
  }

  return (response.result ?? { requests: [], runs: [] }) as BootstrapPayload;
}
