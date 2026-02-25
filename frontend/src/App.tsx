import { useEffect, useMemo, useState } from "react";
import { useKeyboard, useRenderer, useTerminalDimensions } from "@opentui/react";

import { bootstrap, deleteRequest, listRequests, listRuns, recordRun, saveRequest, saveSecret, sendRequest } from "./services/api";
import { methodColor, palette } from "./theme";
import { newRequest, type RequestItem, type RunEntry } from "./types";

type Screen = "MAIN" | "HISTORY" | "EDITOR" | "HELP";
type MainMode = "NORMAL" | "SEARCH" | "COMMAND" | "ACTION" | "DELETE_CONFIRM";
type EditorMode = "NORMAL" | "INSERT" | "COMMAND";
type DragMode = "NONE" | "MAIN_VERTICAL" | "MAIN_HORIZONTAL" | "HISTORY_VERTICAL" | "EDITOR_VERTICAL";

type ResponsePreview = {
  requestName: string;
  requestId: string;
  method: string;
  url: string;
  statusCode: number;
  durationMs: number;
  error: string;
  at: string;
  body: string;
};

type EditorFieldKey =
  | "name"
  | "method"
  | "url"
  | "header_key"
  | "header_value"
  | "auth_type"
  | "auth_secret_ref"
  | "auth_key_name"
  | "auth_location"
  | "auth_username";

type EditorField = {
  key: EditorFieldKey;
  label: string;
  editable: boolean;
};

const METHODS = ["GET", "POST", "PUT", "PATCH", "DELETE"] as const;

const EDITOR_FIELDS: EditorField[] = [
  { key: "name", label: "Name", editable: true },
  { key: "method", label: "Method", editable: false },
  { key: "url", label: "URL", editable: true },
  { key: "header_key", label: "Header Key", editable: true },
  { key: "header_value", label: "Header Value", editable: true },
  { key: "auth_type", label: "Auth Type", editable: true },
  { key: "auth_secret_ref", label: "Secret Ref", editable: true },
  { key: "auth_key_name", label: "Auth Key Name", editable: true },
  { key: "auth_location", label: "Auth Location", editable: true },
  { key: "auth_username", label: "Auth Username", editable: true },
];

const AUTH_FIELD_INDEX = 5;

const MAIN_STATUS_HINT =
  "j/k move | / search | : command | Enter actions | E edit | d delete | ZZ/ZQ quit | { } req body | [ ] resp body | drag";

const HISTORY_STATUS_HINT = "j/k move | r replay | H/L resize | { } detail | mouse drag | Esc back";
const EDITOR_STATUS_HINT = "j/k field | h/l method | i edit | : cmd | Ctrl+s save | Esc cancel | { } body";

function trimTo(text: string, width: number): string {
  if (text.length <= width) {
    return text;
  }
  return `${text.slice(0, Math.max(0, width - 3))}...`;
}

function printableChar(key: { sequence?: string; ctrl?: boolean; meta?: boolean; option?: boolean }): string | null {
  if (key.ctrl || key.meta || key.option) {
    return null;
  }
  const seq = key.sequence ?? "";
  if (seq.length !== 1) {
    return null;
  }
  const code = seq.charCodeAt(0);
  if (code < 32 || code === 127) {
    return null;
  }
  return seq;
}

function backspacePressed(key: { name?: string; sequence?: string }): boolean {
  return key.name === "backspace" || key.sequence === "\x7f";
}

function bodySlice(text: string, offset: number, maxLines: number): string {
  const lines = text.split(/\r?\n/);
  if (lines.length === 0) {
    return "";
  }
  const start = Math.max(0, Math.min(offset, lines.length - 1));
  return lines.slice(start, start + maxLines).join("\n");
}

function nowIsoNoMillis(): string {
  return new Date().toISOString().replace(/\.\d{3}Z$/, "Z");
}

function clamp(value: number, min: number, max: number): number {
  return Math.max(min, Math.min(max, value));
}

function buildRequestSnapshot(req: RequestItem): string {
  return [
    `name: ${req.name || "(unnamed)"}`,
    `method: ${req.method}`,
    `url: ${req.url}`,
    `auth: ${req.auth_type || "none"}`,
    `secret_ref: ${req.auth_secret_ref || "(none)"}`,
    `auth_key_name: ${req.auth_key_name || "(none)"}`,
    `auth_location: ${req.auth_location || "(none)"}`,
    `auth_username: ${req.auth_username || "(none)"}`,
    req.header_key || req.header_value ? `header: ${req.header_key}: ${req.header_value}` : "header: none",
    "body:",
    req.body || "(empty)",
  ].join("\n");
}

function methodWithDelta(method: string, delta: number): string {
  const upper = method.toUpperCase();
  const idx = METHODS.findIndex((m) => m === upper);
  const current = idx >= 0 ? idx : 0;
  const next = (current + delta + METHODS.length) % METHODS.length;
  return METHODS[next];
}

function editorPrompt(mode: EditorMode, input: string, commandInput: string, currentLabel: string): string {
  if (mode === "INSERT") {
    return `INSERT ${currentLabel}: ${input}`;
  }
  if (mode === "COMMAND") {
    return `:${commandInput}`;
  }
  return EDITOR_STATUS_HINT;
}

function mainPrompt(mode: MainMode, commandInput: string, searchInput: string, deleteName: string): string {
  if (mode === "SEARCH") {
    return `/${searchInput}`;
  }
  if (mode === "COMMAND") {
    return `:${commandInput}`;
  }
  if (mode === "ACTION") {
    return "Action: y send | e edit body | a auth editor | Esc cancel";
  }
  if (mode === "DELETE_CONFIRM") {
    return `Delete '${deleteName}'? y/n`;
  }
  return MAIN_STATUS_HINT;
}

function requestFieldValue(req: RequestItem, key: EditorFieldKey): string {
  return req[key] ?? "";
}

function setRequestField(req: RequestItem, key: EditorFieldKey, value: string): RequestItem {
  return {
    ...req,
    [key]: value,
  };
}

export function App() {
  const renderer = useRenderer();
  const { width: termWidth, height: termHeight } = useTerminalDimensions();

  const [screen, setScreen] = useState<Screen>("MAIN");
  const [mode, setMode] = useState<MainMode>("NORMAL");
  const [editorMode, setEditorMode] = useState<EditorMode>("NORMAL");

  const [requests, setRequests] = useState<RequestItem[]>([]);
  const [selected, setSelected] = useState(0);
  const [filter, setFilter] = useState("");
  const [searchInput, setSearchInput] = useState("");
  const [searchSnapshot, setSearchSnapshot] = useState("");
  const [commandInput, setCommandInput] = useState("");

  const [historyRuns, setHistoryRuns] = useState<RunEntry[]>([]);
  const [historySelected, setHistorySelected] = useState(0);
  const [historyDetailScroll, setHistoryDetailScroll] = useState(0);

  const [editorDraft, setEditorDraft] = useState<RequestItem | null>(null);
  const [editorField, setEditorField] = useState(0);
  const [editorInput, setEditorInput] = useState("");
  const [editorCommandInput, setEditorCommandInput] = useState("");
  const [editorBodyScroll, setEditorBodyScroll] = useState(0);

  const [status, setStatus] = useState(MAIN_STATUS_HINT);
  const [statusIsError, setStatusIsError] = useState(false);
  const [pendingG, setPendingG] = useState(false);
  const [pendingZ, setPendingZ] = useState(false);

  const [mainSplitRatio, setMainSplitRatio] = useState(0.66);
  const [mainResponseRatio, setMainResponseRatio] = useState(0.28);
  const [historySplitRatio, setHistorySplitRatio] = useState(0.42);
  const [editorSplitRatio, setEditorSplitRatio] = useState(0.5);
  const [dragMode, setDragMode] = useState<DragMode>("NONE");

  const [requestBodyScroll, setRequestBodyScroll] = useState(0);
  const [responseBodyScroll, setResponseBodyScroll] = useState(0);
  const [lastResponse, setLastResponse] = useState<ResponsePreview | null>(null);

  function setStatusInfo(message: string): void {
    setStatus(message);
    setStatusIsError(false);
  }

  function setStatusError(message: string): void {
    setStatus(message);
    setStatusIsError(true);
  }

  async function reloadRequests(selectId?: string): Promise<void> {
    const items = await listRequests();
    setRequests(items);
    setSelected((prev) => {
      if (selectId) {
        const idx = items.findIndex((item) => item.id === selectId);
        if (idx >= 0) {
          return idx;
        }
      }
      if (items.length === 0) {
        return 0;
      }
      return clamp(prev, 0, items.length - 1);
    });
  }

  async function reloadHistory(selectRunId?: number): Promise<void> {
    const runs = await listRuns(200);
    setHistoryRuns(runs);
    setHistorySelected((prev) => {
      if (selectRunId != null) {
        const idx = runs.findIndex((run) => run.id === selectRunId);
        if (idx >= 0) {
          return idx;
        }
      }
      if (runs.length === 0) {
        return 0;
      }
      return clamp(prev, 0, runs.length - 1);
    });
  }

  useEffect(() => {
    bootstrap()
      .then((data) => {
        setRequests(data.requests ?? []);
        setHistoryRuns(data.runs ?? []);
        setStatusInfo(`Loaded ${data.requests?.length ?? 0} request(s).`);
      })
      .catch((err) => {
        setStatusError(err instanceof Error ? err.message : String(err));
      });
  }, []);

  const visible = useMemo(() => {
    const needle = filter.trim().toLowerCase();
    if (!needle) {
      return requests;
    }
    return requests.filter((r) => `${r.name} ${r.method} ${r.url}`.toLowerCase().includes(needle));
  }, [requests, filter]);

  const selectedItem = visible[selected] ?? null;
  const selectedRun = historyRuns[historySelected] ?? null;

  useEffect(() => {
    setSelected((prev) => {
      if (visible.length === 0) {
        return 0;
      }
      return clamp(prev, 0, visible.length - 1);
    });
  }, [visible]);

  useEffect(() => {
    setRequestBodyScroll(0);
    setPendingG(false);
    setPendingZ(false);
  }, [selectedItem?.id]);

  useEffect(() => {
    setHistoryDetailScroll(0);
  }, [selectedRun?.id]);

  function openEditor(request: RequestItem, initialField: number): void {
    setEditorDraft({ ...request });
    setEditorField(clamp(initialField, 0, EDITOR_FIELDS.length - 1));
    setEditorInput("");
    setEditorCommandInput("");
    setEditorBodyScroll(0);
    setEditorMode("NORMAL");
    setScreen("EDITOR");
    setStatusInfo("Editor opened.");
  }

  function closeEditorWithStatus(message: string, isError = false): void {
    setScreen("MAIN");
    setEditorMode("NORMAL");
    if (isError) {
      setStatusError(message);
    } else {
      setStatusInfo(message);
    }
  }

  async function saveEditorDraftAndClose(): Promise<void> {
    if (!editorDraft) {
      closeEditorWithStatus("No draft to save.", true);
      return;
    }
    if (!editorDraft.url.trim()) {
      setStatusError("URL is required before save.");
      return;
    }

    try {
      const saved = await saveRequest(editorDraft);
      await reloadRequests(saved.id);
      closeEditorWithStatus(`Saved ${saved.name}.`);
    } catch (err) {
      closeEditorWithStatus(`Save failed: ${err instanceof Error ? err.message : String(err)}`, true);
    }
  }

  async function executeMainCommand(raw: string): Promise<void> {
    const line = raw.trim();
    if (!line) {
      setStatusInfo(MAIN_STATUS_HINT);
      return;
    }

    const parts = line.split(/\s+/);
    const cmd = parts[0]?.toLowerCase() ?? "";

    if (cmd === "q") {
      renderer.destroy();
      return;
    }

    if (cmd === "help") {
      setScreen("HELP");
      setStatusInfo("Help screen opened.");
      return;
    }

    if (cmd === "history") {
      try {
        await reloadHistory();
        setScreen("HISTORY");
        setStatusInfo("History loaded.");
      } catch (err) {
        setStatusError(`Failed to load history: ${err instanceof Error ? err.message : String(err)}`);
      }
      return;
    }

    if (cmd === "new") {
      const method = (parts[1] ?? "GET").toUpperCase();
      const url = parts[2] ?? "";
      openEditor(newRequest(method, url), 2);
      return;
    }

    if (cmd === "edit") {
      if (!selectedItem) {
        setStatusError("No selected request to edit.");
        return;
      }
      openEditor(selectedItem, 0);
      return;
    }

    if (cmd === "import" || cmd === "export") {
      setStatusInfo(`${cmd} parity is planned in upcoming backend migration steps.`);
      return;
    }

    setStatusError(`Unknown command: ${line}`);
  }

  async function runRequest(req: RequestItem): Promise<void> {
    try {
      const sent = await sendRequest(req);
      const at = nowIsoNoMillis();

      const preview: ResponsePreview = {
        requestName: req.name,
        requestId: req.id,
        method: req.method,
        url: req.url,
        statusCode: sent.status_code,
        durationMs: sent.duration_ms,
        error: sent.error,
        at,
        body: sent.body,
      };
      setLastResponse(preview);
      setResponseBodyScroll(0);

      const run: RunEntry = {
        id: 0,
        request_id: req.id,
        request_name: req.name,
        method: req.method,
        url: req.url,
        status_code: sent.status_code,
        duration_ms: sent.duration_ms,
        error: sent.error,
        created_at: at,
        request_snapshot: buildRequestSnapshot(req),
        response_body: sent.body,
      };
      await recordRun(run);

      if (screen === "HISTORY") {
        await reloadHistory();
      }

      if (sent.error) {
        setStatusError(`Request finished with error: ${sent.error}`);
      } else {
        setStatusInfo(`Sent ${req.method} ${req.url} (${sent.status_code}, ${sent.duration_ms}ms)`);
      }
    } catch (err) {
      setStatusError(`Failed to send request: ${err instanceof Error ? err.message : String(err)}`);
    }
  }

  async function runSelectedRequest(): Promise<void> {
    if (!selectedItem) {
      setStatusError("No request selected.");
      return;
    }
    await runRequest(selectedItem);
  }

  async function replaySelectedRun(): Promise<void> {
    if (!selectedRun) {
      setStatusError("No run selected.");
      return;
    }

    try {
      const freshRequests = await listRequests();
      const req = freshRequests.find((item) => item.id === selectedRun.request_id);
      if (!req) {
        setStatusError(`Request no longer exists for run ${selectedRun.id}.`);
        return;
      }
      await runRequest(req);
      setScreen("MAIN");
      setStatusInfo(`Replayed ${req.name}.`);
    } catch (err) {
      setStatusError(`Replay failed: ${err instanceof Error ? err.message : String(err)}`);
    }
  }

  function beginDrag(next: DragMode): void {
    setDragMode(next);
  }

  function stopDrag(): void {
    if (dragMode !== "NONE") {
      setDragMode("NONE");
    }
  }

  function handleMouseDrag(event: { x?: number; y?: number }): void {
    const x = event.x ?? 0;
    const y = event.y ?? 0;
    if (termWidth <= 0 || termHeight <= 0) {
      return;
    }

    if (dragMode === "MAIN_VERTICAL") {
      const ratio = clamp(x / termWidth, 0.2, 0.8);
      setMainSplitRatio(ratio);
      setStatusInfo(`Resize: left=${Math.round(ratio * 100)}% response=${Math.round(mainResponseRatio * 100)}%`);
      return;
    }

    if (dragMode === "MAIN_HORIZONTAL") {
      const responseRatio = clamp((termHeight - y) / termHeight, 0.15, 0.7);
      setMainResponseRatio(responseRatio);
      setStatusInfo(`Resize: left=${Math.round(mainSplitRatio * 100)}% response=${Math.round(responseRatio * 100)}%`);
      return;
    }

    if (dragMode === "HISTORY_VERTICAL") {
      const ratio = clamp(x / termWidth, 0.24, 0.76);
      setHistorySplitRatio(ratio);
      setStatusInfo(`History resize: left=${Math.round(ratio * 100)}%`);
      return;
    }

    if (dragMode === "EDITOR_VERTICAL") {
      const ratio = clamp(x / termWidth, 0.28, 0.78);
      setEditorSplitRatio(ratio);
      setStatusInfo(`Editor resize: left=${Math.round(ratio * 100)}%`);
    }
  }

  useKeyboard((key) => {
    const ch = printableChar(key);

    if (screen === "HELP") {
      if (key.name === "escape" || ch === "q") {
        setScreen("MAIN");
        setStatusInfo(MAIN_STATUS_HINT);
      }
      return;
    }

    if (screen === "HISTORY") {
      if (key.name === "escape") {
        setScreen("MAIN");
        setStatusInfo(MAIN_STATUS_HINT);
        return;
      }
      if (key.name === "down" || ch === "j") {
        setHistorySelected((prev) => clamp(prev + 1, 0, Math.max(0, historyRuns.length - 1)));
        return;
      }
      if (key.name === "up" || ch === "k") {
        setHistorySelected((prev) => clamp(prev - 1, 0, Math.max(0, historyRuns.length - 1)));
        return;
      }
      if (ch === "r") {
        void replaySelectedRun();
        return;
      }
      if (ch === "H") {
        setHistorySplitRatio((prev) => clamp(prev - 0.03, 0.24, 0.76));
        return;
      }
      if (ch === "L") {
        setHistorySplitRatio((prev) => clamp(prev + 0.03, 0.24, 0.76));
        return;
      }
      if (ch === "{") {
        setHistoryDetailScroll((prev) => Math.max(0, prev - 1));
        return;
      }
      if (ch === "}") {
        setHistoryDetailScroll((prev) => prev + 1);
      }
      return;
    }

    if (screen === "EDITOR") {
      if (!editorDraft) {
        closeEditorWithStatus("Editor closed: no draft.", true);
        return;
      }

      const currentField = EDITOR_FIELDS[editorField];

      if (editorMode === "INSERT") {
        if (key.name === "escape") {
          setEditorMode("NORMAL");
          setEditorInput("");
          return;
        }
        if (key.name === "enter" || key.name === "return") {
          setEditorDraft((prev) => {
            if (!prev) {
              return prev;
            }
            return setRequestField(prev, currentField.key, editorInput);
          });
          setEditorMode("NORMAL");
          setEditorInput("");
          return;
        }
        if (backspacePressed(key)) {
          setEditorInput((prev) => prev.slice(0, -1));
          return;
        }
        if (ch) {
          setEditorInput((prev) => `${prev}${ch}`);
        }
        return;
      }

      if (editorMode === "COMMAND") {
        if (key.name === "escape") {
          setEditorMode("NORMAL");
          setEditorCommandInput("");
          return;
        }
        if (key.name === "enter" || key.name === "return") {
          const line = editorCommandInput.trim();
          setEditorCommandInput("");
          setEditorMode("NORMAL");

          if (line === "w" || line === "wq") {
            void saveEditorDraftAndClose();
            return;
          }
          if (line === "q") {
            closeEditorWithStatus("Editor cancelled.");
            return;
          }
          if (line.startsWith("secret ")) {
            const value = line.slice("secret ".length);
            if (!value.trim()) {
              setStatusError("Usage: :secret VALUE");
              return;
            }
            const secretRef = editorDraft.auth_secret_ref.trim();
            if (!secretRef) {
              setStatusError("Set Secret Ref before :secret VALUE");
              return;
            }
            void saveSecret(secretRef, value)
              .then(() => setStatusInfo(`Stored secret for ref '${secretRef}'.`))
              .catch((err) => setStatusError(`Secret write failed: ${err instanceof Error ? err.message : String(err)}`));
            return;
          }

          setStatusError(`Unknown editor command: ${line}`);
          return;
        }
        if (backspacePressed(key)) {
          setEditorCommandInput((prev) => prev.slice(0, -1));
          return;
        }
        if (ch) {
          setEditorCommandInput((prev) => `${prev}${ch}`);
        }
        return;
      }

      if (key.ctrl && key.name === "s") {
        void saveEditorDraftAndClose();
        return;
      }
      if (key.name === "escape") {
        closeEditorWithStatus("Editor cancelled.");
        return;
      }
      if (key.name === "down" || ch === "j") {
        setEditorField((prev) => clamp(prev + 1, 0, EDITOR_FIELDS.length - 1));
        return;
      }
      if (key.name === "up" || ch === "k") {
        setEditorField((prev) => clamp(prev - 1, 0, EDITOR_FIELDS.length - 1));
        return;
      }
      if (ch === "h" && currentField.key === "method") {
        setEditorDraft((prev) => {
          if (!prev) {
            return prev;
          }
          return { ...prev, method: methodWithDelta(prev.method, -1) };
        });
        return;
      }
      if (ch === "l" && currentField.key === "method") {
        setEditorDraft((prev) => {
          if (!prev) {
            return prev;
          }
          return { ...prev, method: methodWithDelta(prev.method, 1) };
        });
        return;
      }
      if (key.name === "enter" || key.name === "return" || ch === "i") {
        if (currentField.editable) {
          setEditorInput(requestFieldValue(editorDraft, currentField.key));
          setEditorMode("INSERT");
        }
        return;
      }
      if (ch === ":") {
        setEditorCommandInput("");
        setEditorMode("COMMAND");
        return;
      }
      if (ch === "e") {
        setStatusInfo("External body editor will be wired next.");
        return;
      }
      if (ch === "{") {
        setEditorBodyScroll((prev) => Math.max(0, prev - 1));
        return;
      }
      if (ch === "}") {
        setEditorBodyScroll((prev) => prev + 1);
      }
      return;
    }

    if (mode === "SEARCH") {
      if (key.name === "escape") {
        setFilter(searchSnapshot);
        setSearchInput(searchSnapshot);
        setMode("NORMAL");
        setStatusInfo(MAIN_STATUS_HINT);
        return;
      }
      if (key.name === "enter" || key.name === "return") {
        setMode("NORMAL");
        setStatusInfo(`Filter locked: ${filter || "(none)"}`);
        return;
      }
      if (backspacePressed(key)) {
        setSearchInput((prev) => {
          const next = prev.slice(0, -1);
          setFilter(next);
          return next;
        });
        return;
      }
      if (ch) {
        setSearchInput((prev) => {
          const next = `${prev}${ch}`;
          setFilter(next);
          return next;
        });
      }
      return;
    }

    if (mode === "COMMAND") {
      if (key.name === "escape") {
        setMode("NORMAL");
        setCommandInput("");
        setStatusInfo(MAIN_STATUS_HINT);
        return;
      }
      if (key.name === "enter" || key.name === "return") {
        const cmd = commandInput;
        setCommandInput("");
        setMode("NORMAL");
        void executeMainCommand(cmd);
        return;
      }
      if (backspacePressed(key)) {
        setCommandInput((prev) => prev.slice(0, -1));
        return;
      }
      if (ch) {
        setCommandInput((prev) => `${prev}${ch}`);
      }
      return;
    }

    if (mode === "ACTION") {
      if (key.name === "escape" || ch === "n") {
        setMode("NORMAL");
        setStatusInfo(MAIN_STATUS_HINT);
        return;
      }
      if (ch === "y") {
        setMode("NORMAL");
        void runSelectedRequest();
        return;
      }
      if (ch === "e") {
        setMode("NORMAL");
        setStatusInfo("Body external editor wiring pending.");
        return;
      }
      if (ch === "a") {
        setMode("NORMAL");
        if (selectedItem) {
          openEditor(selectedItem, AUTH_FIELD_INDEX);
        }
      }
      return;
    }

    if (mode === "DELETE_CONFIRM") {
      if (key.name === "escape" || ch === "n") {
        setMode("NORMAL");
        setStatusInfo("Delete cancelled.");
        return;
      }
      if (ch === "y") {
        if (!selectedItem) {
          setMode("NORMAL");
          setStatusError("No selected request to delete.");
          return;
        }
        const deletingId = selectedItem.id;
        setMode("NORMAL");
        void deleteRequest(deletingId)
          .then(async () => {
            await reloadRequests();
            setStatusInfo("Request deleted.");
          })
          .catch((err) => setStatusError(err instanceof Error ? err.message : String(err)));
      }
      return;
    }

    if (pendingZ && (ch === "Z" || ch === "Q")) {
      renderer.destroy();
      return;
    }

    if (ch !== "g") {
      setPendingG(false);
    }
    if (ch !== "Z") {
      setPendingZ(false);
    }

    if (key.name === "escape") {
      setFilter("");
      setStatusInfo(MAIN_STATUS_HINT);
      return;
    }
    if (key.name === "enter" || key.name === "return") {
      setMode("ACTION");
      return;
    }
    if (key.name === "down" || ch === "j") {
      setSelected((prev) => clamp(prev + 1, 0, Math.max(0, visible.length - 1)));
      return;
    }
    if (key.name === "up" || ch === "k") {
      setSelected((prev) => clamp(prev - 1, 0, Math.max(0, visible.length - 1)));
      return;
    }

    if (ch === "H") {
      setMainSplitRatio((prev) => clamp(prev - 0.03, 0.2, 0.8));
      return;
    }
    if (ch === "L") {
      setMainSplitRatio((prev) => clamp(prev + 0.03, 0.2, 0.8));
      return;
    }
    if (ch === "K") {
      setMainResponseRatio((prev) => clamp(prev - 0.03, 0.15, 0.7));
      return;
    }
    if (ch === "J") {
      setMainResponseRatio((prev) => clamp(prev + 0.03, 0.15, 0.7));
      return;
    }

    if (ch === "g") {
      if (pendingG) {
        setSelected(0);
        setPendingG(false);
      } else {
        setPendingG(true);
      }
      return;
    }
    if (ch === "G") {
      setSelected(Math.max(0, visible.length - 1));
      return;
    }
    if (ch === "Z") {
      setPendingZ(true);
      return;
    }

    if (ch === "/") {
      setSearchSnapshot(filter);
      setSearchInput(filter);
      setMode("SEARCH");
      return;
    }
    if (ch === ":") {
      setCommandInput("");
      setMode("COMMAND");
      return;
    }
    if (ch === "d") {
      if (selectedItem) {
        setMode("DELETE_CONFIRM");
      } else {
        setStatusError("No selected request to delete.");
      }
      return;
    }
    if (ch === "E") {
      if (!selectedItem) {
        setStatusError("No selected request to edit.");
        return;
      }
      openEditor(selectedItem, 0);
      return;
    }
    if (ch === "{") {
      setRequestBodyScroll((prev) => Math.max(0, prev - 1));
      return;
    }
    if (ch === "}") {
      setRequestBodyScroll((prev) => prev + 1);
      return;
    }
    if (ch === "[") {
      setResponseBodyScroll((prev) => Math.max(0, prev - 1));
      return;
    }
    if (ch === "]") {
      setResponseBodyScroll((prev) => prev + 1);
    }
  });

  const requestBodyVisible = bodySlice(selectedItem?.body || "(empty)", requestBodyScroll, 220);
  const responseBodyVisible = bodySlice(lastResponse?.body || "", responseBodyScroll, 220);
  const historyDetailVisible = bodySlice(
    `${selectedRun?.request_snapshot || ""}\n\nresponse body:\n${selectedRun?.response_body || ""}`,
    historyDetailScroll,
    220,
  );
  const editorBodyVisible = bodySlice(editorDraft?.body || "(empty)", editorBodyScroll, 220);

  const showMainRight = termWidth >= 90;
  const showHistoryRight = termWidth >= 84;
  const showEditorRight = termWidth >= 90;

  const mainLeftPct = Math.round(mainSplitRatio * 100);
  const historyLeftPct = Math.round(historySplitRatio * 100);
  const editorLeftPct = Math.round(editorSplitRatio * 100);

  const topGrow = Math.max(1, 100 - Math.round(mainResponseRatio * 100));
  const bottomGrow = Math.max(1, Math.round(mainResponseRatio * 100));

  function renderMainScreen() {
    return (
      <box flexGrow={1} flexDirection="column">
        <box flexGrow={topGrow} flexDirection="row" border borderColor={palette.border}>
          <box width={showMainRight ? `${mainLeftPct}%` : "100%"} paddingX={1} paddingTop={1} backgroundColor={palette.panel}>
            <text fg={palette.section}>Requests</text>
            <text fg={palette.hint}>{trimTo(`Filter: ${filter || "(none)"}`, 64)}</text>
            <scrollbox flexGrow={1} marginTop={1}>
              {visible.map((req, idx) => {
                const active = idx === selected;
                return (
                  <text key={req.id} fg={active ? palette.ok : palette.hint}>
                    <span fg={active ? palette.ok : methodColor(req.method)}>{active ? ">" : " "}</span>
                    <span fg={active ? palette.ok : methodColor(req.method)}>{req.method.padEnd(6, " ")}</span>
                    <span> {trimTo(req.name, 36)}</span>
                  </text>
                );
              })}
              {visible.length === 0 ? <text fg={palette.warn}>No matching requests.</text> : null}
            </scrollbox>
          </box>

          {showMainRight ? (
            <box
              width={1}
              backgroundColor={palette.border}
              onMouseDown={() => beginDrag("MAIN_VERTICAL")}
              onMouseDrag={() => beginDrag("MAIN_VERTICAL")}
            >
              <text fg={palette.bg}>|</text>
            </box>
          ) : null}

          {showMainRight ? (
            <box flexGrow={1} border borderColor={palette.border} paddingX={1} paddingTop={1}>
              <text fg={palette.section}>Request Preview</text>
              {selectedItem ? (
                <box marginTop={1} flexDirection="column" gap={0}>
                  <text>
                    <span fg={palette.section}>name: </span>
                    <span fg={palette.hint}>{selectedItem.name}</span>
                  </text>
                  <text>
                    <span fg={palette.section}>method: </span>
                    <span fg={methodColor(selectedItem.method)}>{selectedItem.method}</span>
                  </text>
                  <text>
                    <span fg={palette.section}>url: </span>
                    <span fg={palette.hint}>{selectedItem.url}</span>
                  </text>
                  <text fg={palette.section}>Body</text>
                  <scrollbox flexGrow={1} border borderColor={palette.border} paddingX={1} paddingY={0}>
                    <text fg={palette.hint}>{requestBodyVisible}</text>
                  </scrollbox>
                </box>
              ) : (
                <text fg={palette.warn}>No request selected.</text>
              )}
            </box>
          ) : null}
        </box>

        <box
          height={1}
          backgroundColor={palette.border}
          onMouseDown={() => beginDrag("MAIN_HORIZONTAL")}
          onMouseDrag={() => beginDrag("MAIN_HORIZONTAL")}
        >
          <text fg={palette.bg}>-</text>
        </box>

        <box flexGrow={bottomGrow} border borderColor={palette.border} paddingX={1} paddingTop={1}>
          <text fg={palette.section}>Response</text>
          {lastResponse ? (
            <box flexDirection="column">
              <text>
                <span fg={palette.section}>request: </span>
                <span fg={palette.hint}>{lastResponse.requestName}</span>
                <span fg={palette.hint}> ({lastResponse.requestId})</span>
              </text>
              <text>
                <span fg={palette.section}>method/url: </span>
                <span fg={methodColor(lastResponse.method)}>{lastResponse.method}</span>
                <span fg={palette.hint}> {lastResponse.url}</span>
              </text>
              <text>
                <span fg={palette.section}>at/status/ms: </span>
                <span fg={palette.hint}>
                  {lastResponse.at} / {lastResponse.statusCode} / {lastResponse.durationMs}
                </span>
              </text>
              {lastResponse.error ? (
                <text>
                  <span fg={palette.section}>error: </span>
                  <span fg={palette.error}>{lastResponse.error}</span>
                </text>
              ) : null}
              <text fg={palette.section}>Body</text>
              <scrollbox flexGrow={1} border borderColor={palette.border} paddingX={1} paddingY={0}>
                <text fg={palette.hint}>{responseBodyVisible || "(empty)"}</text>
              </scrollbox>
            </box>
          ) : (
            <text fg={palette.hint}>No response yet. Select a request, press Enter, then y.</text>
          )}
        </box>
      </box>
    );
  }

  function renderHistoryScreen() {
    return (
      <box flexGrow={1} border borderColor={palette.border} flexDirection="row">
        <box width={showHistoryRight ? `${historyLeftPct}%` : "100%"} paddingX={1} paddingTop={1} backgroundColor={palette.panel}>
          <text fg={palette.section}>History</text>
          <scrollbox flexGrow={1} marginTop={1}>
            {historyRuns.map((run, idx) => {
              const active = idx === historySelected;
              const codeColor = run.status_code >= 500 ? palette.error : run.status_code >= 400 ? palette.warn : palette.ok;
              return (
                <text key={`${run.id}`} fg={active ? palette.ok : palette.hint}>
                  <span fg={active ? palette.ok : codeColor}>{active ? ">" : " "}</span>
                  <span fg={active ? palette.ok : methodColor(run.method)}>{run.method.padEnd(6, " ")}</span>
                  <span> {trimTo(run.request_name || run.request_id, 28)}</span>
                  <span fg={active ? palette.ok : codeColor}> [{run.status_code}]</span>
                </text>
              );
            })}
            {historyRuns.length === 0 ? <text fg={palette.warn}>No history runs yet.</text> : null}
          </scrollbox>
        </box>

        {showHistoryRight ? (
          <box width={1} backgroundColor={palette.border} onMouseDown={() => beginDrag("HISTORY_VERTICAL")}>
            <text fg={palette.bg}>|</text>
          </box>
        ) : null}

        {showHistoryRight ? (
          <box flexGrow={1} border borderColor={palette.border} paddingX={1} paddingTop={1}>
            <text fg={palette.section}>Run Detail</text>
            {selectedRun ? (
              <box flexDirection="column">
                <text>
                  <span fg={palette.section}>id/at: </span>
                  <span fg={palette.hint}>#{selectedRun.id} {selectedRun.created_at}</span>
                </text>
                <text>
                  <span fg={palette.section}>method/url: </span>
                  <span fg={methodColor(selectedRun.method)}>{selectedRun.method}</span>
                  <span fg={palette.hint}> {selectedRun.url}</span>
                </text>
                <text>
                  <span fg={palette.section}>status/ms: </span>
                  <span fg={palette.hint}>{selectedRun.status_code} / {selectedRun.duration_ms}</span>
                </text>
                {selectedRun.error ? (
                  <text>
                    <span fg={palette.section}>error: </span>
                    <span fg={palette.error}>{selectedRun.error}</span>
                  </text>
                ) : null}
                <scrollbox flexGrow={1} border borderColor={palette.border} paddingX={1} paddingY={0}>
                  <text fg={palette.hint}>{historyDetailVisible || "(empty)"}</text>
                </scrollbox>
              </box>
            ) : (
              <text fg={palette.warn}>No run selected.</text>
            )}
          </box>
        ) : null}
      </box>
    );
  }

  function renderEditorScreen() {
    const draft = editorDraft;
    const currentField = EDITOR_FIELDS[editorField];
    return (
      <box flexGrow={1} border borderColor={palette.border} flexDirection="row">
        <box width={showEditorRight ? `${editorLeftPct}%` : "100%"} paddingX={1} paddingTop={1} backgroundColor={palette.panel}>
          <text fg={palette.section}>Editor</text>
          {draft ? (
            <scrollbox flexGrow={1} marginTop={1}>
              {EDITOR_FIELDS.map((field, idx) => {
                const active = idx === editorField;
                const value = requestFieldValue(draft, field.key);
                return (
                  <text key={field.key} fg={active ? palette.ok : palette.hint}>
                    <span fg={active ? palette.ok : palette.section}>{active ? "> " : "  "}{field.label}: </span>
                    <span fg={field.key === "method" ? methodColor(value) : active ? palette.ok : palette.hint}>
                      {trimTo(value || "(empty)", 50)}
                    </span>
                  </text>
                );
              })}
            </scrollbox>
          ) : (
            <text fg={palette.warn}>No draft.</text>
          )}
        </box>

        {showEditorRight ? (
          <box width={1} backgroundColor={palette.border} onMouseDown={() => beginDrag("EDITOR_VERTICAL")}>
            <text fg={palette.bg}>|</text>
          </box>
        ) : null}

        {showEditorRight && draft ? (
          <box flexGrow={1} border borderColor={palette.border} paddingX={1} paddingTop={1}>
            <text fg={palette.section}>Preview</text>
            <box marginTop={1} flexDirection="column">
              <text>
                <span fg={palette.section}>name: </span>
                <span fg={palette.hint}>{draft.name}</span>
              </text>
              <text>
                <span fg={palette.section}>method: </span>
                <span fg={methodColor(draft.method)}>{draft.method}</span>
              </text>
              <text>
                <span fg={palette.section}>url: </span>
                <span fg={palette.hint}>{draft.url}</span>
              </text>
              <text fg={palette.section}>Body</text>
              <scrollbox flexGrow={1} border borderColor={palette.border} paddingX={1} paddingY={0}>
                <text fg={palette.hint}>{editorBodyVisible}</text>
              </scrollbox>
            </box>
            <text fg={palette.hint}>Field: {currentField.label}</text>
          </box>
        ) : null}
      </box>
    );
  }

  function renderHelpScreen() {
    return (
      <box flexGrow={1} border borderColor={palette.border} paddingX={1} paddingTop={1}>
        <text fg={palette.section}>Help</text>
        <scrollbox flexGrow={1} marginTop={1}>
          <text fg={palette.hint}>Main:</text>
          <text fg={palette.hint}>  j/k, gg/G, Enter, /, :, d, E, ZZ/ZQ, H/L, K/J, {'{'} {'}'}, [ ]</text>
          <text fg={palette.hint}>Action: y send, e body edit (pending), a auth editor, Esc/n cancel</text>
          <text fg={palette.hint}>History: j/k, r replay, H/L, {'{'} {'}'}, Esc</text>
          <text fg={palette.hint}>Editor: j/k, h/l method, i/Enter edit, :w/:q/:wq, Ctrl+s, Esc</text>
          <text fg={palette.hint}>Press Esc to return.</text>
        </scrollbox>
      </box>
    );
  }

  let prompt = MAIN_STATUS_HINT;
  let modeLabel = `[${screen}]`;

  if (screen === "MAIN") {
    modeLabel = `[MAIN ${mode}]`;
    prompt = mainPrompt(mode, commandInput, searchInput, selectedItem?.name ?? "");
  } else if (screen === "HISTORY") {
    modeLabel = "[HISTORY]";
    prompt = HISTORY_STATUS_HINT;
  } else if (screen === "EDITOR") {
    modeLabel = `[EDITOR ${editorMode}]`;
    const currentLabel = EDITOR_FIELDS[editorField]?.label ?? "Field";
    prompt = editorPrompt(editorMode, editorInput, editorCommandInput, currentLabel);
  } else if (screen === "HELP") {
    modeLabel = "[HELP]";
    prompt = "Esc to return to main";
  }

  return (
    <box
      width="100%"
      height="100%"
      backgroundColor={palette.bg}
      flexDirection="column"
      onMouseDrag={handleMouseDrag}
      onMouseUp={stopDrag}
      onMouseDragEnd={stopDrag}
      onMouseDrop={stopDrag}
    >
      <box height={1} paddingX={1} backgroundColor={palette.panelSoft}>
        <text fg={palette.section}>
          <strong>tuiman</strong>
          <span fg={palette.hint}>  rewrite/typescript-opentui</span>
        </text>
      </box>

      {screen === "MAIN" ? renderMainScreen() : null}
      {screen === "HISTORY" ? renderHistoryScreen() : null}
      {screen === "EDITOR" ? renderEditorScreen() : null}
      {screen === "HELP" ? renderHelpScreen() : null}

      <box height={1} paddingX={1} backgroundColor={palette.panelSoft}>
        <text fg={statusIsError ? palette.error : palette.hint}>
          {modeLabel} {trimTo(prompt, 220)}
        </text>
      </box>
      <box height={1} paddingX={1} backgroundColor={palette.bg}>
        <text fg={statusIsError ? palette.error : palette.hint}>{trimTo(status, 220)}</text>
      </box>
    </box>
  );
}
