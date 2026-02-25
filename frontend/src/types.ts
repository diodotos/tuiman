export type RequestItem = {
  id: string;
  name: string;
  method: string;
  url: string;
  body: string;
  updated_at: string;
};

export type RunEntry = {
  id: number;
  request_id: string;
  request_name: string;
  method: string;
  url: string;
  status_code: number;
  duration_ms: number;
  error: string;
  created_at: string;
  request_snapshot: string;
  response_body: string;
};

export type BootstrapPayload = {
  requests: RequestItem[];
  runs: RunEntry[];
};
