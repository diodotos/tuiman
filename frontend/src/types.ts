export type RequestItem = {
  id: string;
  name: string;
  method: string;
  url: string;
  header_key: string;
  header_value: string;
  body: string;
  auth_type: string;
  auth_secret_ref: string;
  auth_key_name: string;
  auth_location: string;
  auth_username: string;
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

export function newRequest(method = "GET", url = ""): RequestItem {
  return {
    id: "",
    name: "New Request",
    method,
    url,
    header_key: "",
    header_value: "",
    body: "",
    auth_type: "none",
    auth_secret_ref: "",
    auth_key_name: "",
    auth_location: "",
    auth_username: "",
    updated_at: "",
  };
}
