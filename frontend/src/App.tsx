import { readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { spawnSync } from "node:child_process";
import { useEffect, useMemo, useState } from "react";
import { useKeyboard, useRenderer, useTerminalDimensions } from "@opentui/react";

import {
  bootstrap,
  deleteRequest,
  exportRequests,
  importRequests,
  listRequests,
  listRuns,
  recordRun,
  saveRequest,
  saveSecret,
  sendRequest,
} from "./services/api";
import { methodColor, palette } from "./theme";
import { newRequest, type RequestItem, type RunEntry } from "./types";

type Screen = "MAIN" | "HISTORY" | "EDITOR" | "HELP";
type MainMode = "NORMAL" | "SEARCH" | "COMMAND" | "ACTION" | "DELETE_CONFIRM";
type HistoryMode = "NORMAL" | "SEARCH";
type EditorMode = "NORMAL" | "INSERT" | "COMMAND";
type DragMode = "NONE" | "MAIN_VERTICAL" | "MAIN_HORIZONTAL" | "HISTORY_VERTICAL" | "EDITOR_VERTICAL";

type MouseLikeEvent = {
  x?: number;
  y?: number;
  preventDefault?: () => void;
};

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

const MAIN_IDLE_HINT = ":help for keybinds and commands";
const HISTORY_IDLE_HINT = "History: / filter | r replay | K/J split | { } req body | [ ] resp body | Esc back";
const EDITOR_IDLE_HINT = "Editor: :help for keybinds";

const MAIN_MIN_LEFT_COLS = 20;
const MAIN_MIN_RIGHT_COLS = 12;
const HISTORY_MIN_LEFT_COLS = 22;
const HISTORY_MIN_RIGHT_COLS = 20;
const EDITOR_MIN_LEFT_COLS = 24;
const EDITOR_MIN_RIGHT_COLS = 18;
const MAIN_MIN_TOP_ROWS = 8;
const MAIN_MIN_BOTTOM_ROWS = 6;

function trimTo(text: string, width: number): string {
  if (text.length <= width) {
    return text;
  }
  return `${text.slice(0, Math.max(0, width - 3))}...`;
}

function fitTo(text: string, width: number): string {
  const maxWidth = Math.max(1, width);
  return trimTo(text, maxWidth).padEnd(maxWidth, " ");
}

function wrapFixedLines(text: string, width: number, maxLines: number): string[] {
  const effectiveWidth = Math.max(1, width);
  const effectiveMaxLines = Math.max(1, maxLines);
  const source = text ?? "";
  const chunks: string[] = [];

  if (source.length === 0) {
    chunks.push(" ".repeat(effectiveWidth));
  } else {
    for (let i = 0; i < source.length; i += effectiveWidth) {
      chunks.push(source.slice(i, i + effectiveWidth).padEnd(effectiveWidth, " "));
    }
  }

  const window = chunks.slice(0, effectiveMaxLines);
  while (window.length < effectiveMaxLines) {
    window.push(" ".repeat(effectiveWidth));
  }
  return window;
}

function wrapLabelValue(label: string, value: string, totalWidth: number, maxLines: number): {
  label: string;
  labelWidth: number;
  valueWidth: number;
  valueLines: string[];
} {
  const labelWidth = `${label} `.length;
  const valueWidth = Math.max(1, totalWidth - labelWidth);
  return {
    label,
    labelWidth,
    valueWidth,
    valueLines: wrapFixedLines(value, valueWidth, maxLines),
  };
}

function isLikelyJson(text: string): boolean {
  const trimmed = text.trimStart();
  return trimmed.startsWith("{") || trimmed.startsWith("[");
}

function responseStatusColor(statusCode?: number): string {
  if (typeof statusCode !== "number") {
    return palette.hint;
  }
  if (statusCode >= 500) {
    return palette.error;
  }
  if (statusCode >= 400) {
    return palette.warn;
  }
  if (statusCode >= 300) {
    return palette.section;
  }
  if (statusCode >= 200) {
    return palette.ok;
  }
  return palette.hint;
}

type JsonToken = { text: string; fg?: string };

type ParsedRequestSnapshot = {
  fields: Record<string, string>;
  body: string;
};

function jsonTokens(line: string): JsonToken[] {
  const tokens: JsonToken[] = [];
  const re = /"(?:\\.|[^"\\])*"(?=\s*:)|"(?:\\.|[^"\\])*"|\b-?\d+(?:\.\d+)?(?:[eE][+-]?\d+)?\b|\btrue\b|\bfalse\b|\bnull\b|[{}\[\],:]/g;
  let cursor = 0;

  for (const match of line.matchAll(re)) {
    const start = match.index ?? 0;
    const end = start + match[0].length;
    if (start > cursor) {
      tokens.push({ text: line.slice(cursor, start) });
    }

    const t = match[0];
    if ((t.startsWith("{") || t.startsWith("[") || t.startsWith("}") || t.startsWith("]"))) {
      tokens.push({ text: t, fg: palette.section });
    } else if (/^"(?:\\.|[^"\\])*"$/.test(t) && line.slice(end).trimStart().startsWith(":")) {
      tokens.push({ text: t, fg: palette.section });
    } else if (t.startsWith('"')) {
      tokens.push({ text: t, fg: palette.jsonString });
    } else if (/^(true|false|null)$/.test(t)) {
      tokens.push({ text: t, fg: palette.warn });
    } else if (/^-?\d/.test(t)) {
      tokens.push({ text: t, fg: palette.warn });
    } else {
      tokens.push({ text: t });
    }

    cursor = end;
  }

  if (cursor < line.length) {
    tokens.push({ text: line.slice(cursor) });
  }

  if (tokens.length === 0) {
    tokens.push({ text: line });
  }
  return tokens;
}

function isTransportFailure(run: RunEntry): boolean {
  return run.status_code <= 0 || run.error.trim().length > 0 && run.status_code <= 0;
}

function historyStatusLabel(run: RunEntry): string {
  return isTransportFailure(run) ? "ERR" : `[${run.status_code}]`;
}

function historyStatusColor(run: RunEntry): string {
  if (isTransportFailure(run)) {
    return palette.error;
  }
  return responseStatusColor(run.status_code);
}

function parseRequestSnapshot(snapshot: string): ParsedRequestSnapshot {
  const raw = snapshot || "";
  const splitToken = "\nbody:\n";
  const splitIndex = raw.indexOf(splitToken);
  const metaPart = splitIndex >= 0 ? raw.slice(0, splitIndex) : raw;
  const bodyPart = splitIndex >= 0 ? raw.slice(splitIndex + splitToken.length) : "";
  const fields: Record<string, string> = {};

  for (const line of metaPart.split("\n")) {
    const trimmed = line.trim();
    if (!trimmed) {
      continue;
    }
    const idx = trimmed.indexOf(":");
    if (idx < 0) {
      continue;
    }
    const key = trimmed.slice(0, idx).trim();
    const value = trimmed.slice(idx + 1).trim();
    fields[key] = value;
  }

  return {
    fields,
    body: bodyPart || "(empty)",
  };
}

function normalizeBottomBarInput(text: string): string {
  const withoutBracketedPasteMarkers = text.replace(/\x1b\[200~/g, "").replace(/\x1b\[201~/g, "");
  const singleLine = withoutBracketedPasteMarkers.replace(/\r\n?/g, "\n").replace(/\n/g, " ");
  let out = "";

  for (const ch of singleLine) {
    const code = ch.charCodeAt(0);
    if (code < 32 || code === 127) {
      continue;
    }
    out += ch;
  }
  return out;
}

function bottomBarInputChunk(key: { sequence?: string; ctrl?: boolean; meta?: boolean; option?: boolean }): string | null {
  if (key.ctrl || key.meta || key.option) {
    return null;
  }
  const normalized = normalizeBottomBarInput(key.sequence ?? "");
  return normalized.length > 0 ? normalized : null;
}

function readSystemClipboardText(): { ok: true; text: string } | { ok: false; error: string } {
  if (process.platform !== "darwin") {
    return { ok: false, error: "System clipboard shortcuts currently supported on macOS only." };
  }
  const proc = spawnSync("pbpaste", [], { encoding: "utf8" });
  if (proc.error) {
    return { ok: false, error: proc.error.message };
  }
  if (proc.status !== 0) {
    return { ok: false, error: (proc.stderr || "pbpaste failed").trim() || "pbpaste failed" };
  }
  return { ok: true, text: proc.stdout || "" };
}

function writeSystemClipboardText(text: string): { ok: true } | { ok: false; error: string } {
  if (process.platform !== "darwin") {
    return { ok: false, error: "System clipboard shortcuts currently supported on macOS only." };
  }
  const proc = spawnSync("pbcopy", [], { input: text, encoding: "utf8" });
  if (proc.error) {
    return { ok: false, error: proc.error.message };
  }
  if (proc.status !== 0) {
    return { ok: false, error: (proc.stderr || "pbcopy failed").trim() || "pbcopy failed" };
  }
  return { ok: true };
}

function printableChar(key: { sequence?: string; ctrl?: boolean; meta?: boolean; option?: boolean }): string | null {
  const chunk = bottomBarInputChunk(key);
  if (!chunk || chunk.length !== 1) {
    return null;
  }
  return chunk;
}

function backspacePressed(key: { name?: string; sequence?: string }): boolean {
  return key.name === "backspace" || key.sequence === "\x7f";
}

function clearInputPressed(key: { name?: string; sequence?: string; ctrl?: boolean }): boolean {
  if (!key.ctrl) {
    return false;
  }
  if (key.name === "backspace" || key.name === "delete") {
    return true;
  }
  return key.sequence === "\x08" || key.sequence === "\x7f" || key.sequence === "\x1b[3;5~";
}

function bodySliceLines(text: string, offset: number, maxLines: number, wrapWidth: number): string[] {
  const effectiveWidth = Math.max(1, wrapWidth);
  const effectiveMaxLines = Math.max(1, maxLines);
  const rawLines = text.split(/\r?\n/);
  const wrappedLines: string[] = [];

  for (const rawLine of rawLines) {
    if (rawLine.length === 0) {
      wrappedLines.push(" ".repeat(effectiveWidth));
      continue;
    }
    for (let i = 0; i < rawLine.length; i += effectiveWidth) {
      wrappedLines.push(rawLine.slice(i, i + effectiveWidth).padEnd(effectiveWidth, " "));
    }
  }

  if (wrappedLines.length === 0) {
    return Array.from({ length: effectiveMaxLines }, () => " ".repeat(effectiveWidth));
  }

  const start = Math.max(0, Math.min(offset, wrappedLines.length - 1));
  const window = wrappedLines.slice(start, start + effectiveMaxLines);
  while (window.length < effectiveMaxLines) {
    window.push(" ".repeat(effectiveWidth));
  }
  return window;
}

function bodySlice(text: string, offset: number, maxLines: number, wrapWidth: number): string {
  return bodySliceLines(text, offset, maxLines, wrapWidth).join("\n");
}

type BodyPaneProps = {
  keyPrefix: string;
  lines: string[];
  visibleText: string;
  hasJson: boolean;
  emptyText?: string;
};

function BodyPane({ keyPrefix, lines, visibleText, hasJson, emptyText }: BodyPaneProps) {
  return (
    <box flexGrow={1} border borderColor={palette.border} paddingX={1} paddingY={0} backgroundColor={palette.bg}>
      {hasJson ? (
        <text>
          {lines.map((line, idx) => (
            <span key={`${keyPrefix}-line-${idx}`} fg={palette.hint}>
              {jsonTokens(line).map((token, tokenIdx) => (
                <span key={`${keyPrefix}-token-${idx}-${tokenIdx}`} fg={token.fg ?? palette.hint}>{token.text}</span>
              ))}
              {idx < lines.length - 1 ? "\n" : ""}
            </span>
          ))}
        </text>
      ) : (
        <text fg={palette.hint}>{visibleText || emptyText || "(empty)"}</text>
      )}
    </box>
  );
}

function nowIsoNoMillis(): string {
  return new Date().toISOString().replace(/\.\d{3}Z$/, "Z");
}

function maybePrettyJsonBody(body: string): { ok: true; body: string } | { ok: false; error: string } {
  const trimmed = body.trimStart();
  if (!(trimmed.startsWith("{") || trimmed.startsWith("["))) {
    return { ok: true, body };
  }

  try {
    const parsed = JSON.parse(body);
    return { ok: true, body: JSON.stringify(parsed, null, 2) };
  } catch (err) {
    return {
      ok: false,
      error: err instanceof Error ? err.message : "invalid JSON body",
    };
  }
}

async function editTextInExternalEditor(initial: string, extension = "txt"): Promise<string | null> {
  const base = `tuiman-${Date.now()}-${Math.random().toString(36).slice(2, 8)}.${extension}`;
  const path = join(tmpdir(), base);
  const editor = process.env.VISUAL || process.env.EDITOR || "vi";
  writeFileSync(path, initial, "utf8");

  try {
    const proc = Bun.spawnSync(["sh", "-lc", `${editor} "$1"`, "sh", path], {
      stdin: "inherit",
      stdout: "inherit",
      stderr: "inherit",
    });

    if (proc.exitCode !== 0) {
      return null;
    }
    return readFileSync(path, "utf8");
  } finally {
    rmSync(path, { force: true });
  }
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
  return EDITOR_IDLE_HINT;
}

function mainPrompt(mode: MainMode, commandInput: string, searchInput: string, searchPrefix: "/" | "?", deleteName: string): string {
  if (mode === "SEARCH") {
    return `${searchPrefix}${searchInput}`;
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
  return MAIN_IDLE_HINT;
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
  const [searchPrefix, setSearchPrefix] = useState<"/" | "?">("/");
  const [commandInput, setCommandInput] = useState("");

  const [historyRuns, setHistoryRuns] = useState<RunEntry[]>([]);
  const [historySelected, setHistorySelected] = useState(0);
  const [historyMode, setHistoryMode] = useState<HistoryMode>("NORMAL");
  const [historyFilter, setHistoryFilter] = useState("");
  const [historySearchInput, setHistorySearchInput] = useState("");
  const [historySearchSnapshot, setHistorySearchSnapshot] = useState("");
  const [historyRequestBodyScroll, setHistoryRequestBodyScroll] = useState(0);
  const [historyResponseBodyScroll, setHistoryResponseBodyScroll] = useState(0);
  const [historyDetailRatio, setHistoryDetailRatio] = useState(0.52);

  const [editorDraft, setEditorDraft] = useState<RequestItem | null>(null);
  const [editorField, setEditorField] = useState(0);
  const [editorInput, setEditorInput] = useState("");
  const [editorCommandInput, setEditorCommandInput] = useState("");
  const [editorBodyScroll, setEditorBodyScroll] = useState(0);

  const [status, setStatus] = useState(MAIN_IDLE_HINT);
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

  function appendClipboardText(append: (chunk: string) => void): void {
    const clip = readSystemClipboardText();
    if (!clip.ok) {
      setStatusError(`Paste failed: ${clip.error}`);
      return;
    }
    const chunk = normalizeBottomBarInput(clip.text);
    if (!chunk) {
      setStatusError("Paste failed: clipboard is empty or non-printable.");
      return;
    }
    append(chunk);
  }

  function copyInputToClipboard(text: string): void {
    const result = writeSystemClipboardText(text);
    if (!result.ok) {
      setStatusError(`Copy failed: ${result.error}`);
      return;
    }
    setStatusInfo("Copied bottom input to system clipboard.");
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

  const historyVisible = useMemo(() => {
    const needle = historyFilter.trim().toLowerCase();
    if (!needle) {
      return historyRuns;
    }
    return historyRuns.filter((run) => {
      const statusLabel = historyStatusLabel(run).toLowerCase();
      return `${run.request_name} ${run.request_id} ${run.method} ${run.url} ${statusLabel} ${run.error}`
        .toLowerCase()
        .includes(needle);
    });
  }, [historyRuns, historyFilter]);

  const selectedItem = visible[selected] ?? null;
  const selectedRun = historyVisible[historySelected] ?? null;

  useEffect(() => {
    setSelected((prev) => {
      if (visible.length === 0) {
        return 0;
      }
      return clamp(prev, 0, visible.length - 1);
    });
  }, [visible]);

  useEffect(() => {
    setHistorySelected((prev) => {
      if (historyVisible.length === 0) {
        return 0;
      }
      return clamp(prev, 0, historyVisible.length - 1);
    });
  }, [historyVisible]);

  useEffect(() => {
    setRequestBodyScroll(0);
    setPendingG(false);
    setPendingZ(false);
  }, [selectedItem?.id]);

  useEffect(() => {
    setHistoryRequestBodyScroll(0);
    setHistoryResponseBodyScroll(0);
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

    const maybeFormatted = maybePrettyJsonBody(editorDraft.body);
    if (!maybeFormatted.ok) {
      setStatusError(`Body JSON invalid: ${maybeFormatted.error}`);
      return;
    }

    try {
      const saved = await saveRequest({
        ...editorDraft,
        body: maybeFormatted.body,
      });
      await reloadRequests(saved.id);
      closeEditorWithStatus(`Saved ${saved.name}.`);
    } catch (err) {
      closeEditorWithStatus(`Save failed: ${err instanceof Error ? err.message : String(err)}`, true);
    }
  }

  async function editSelectedBodyExternally(): Promise<void> {
    if (!selectedItem) {
      setStatusError("No selected request for body edit.");
      return;
    }

    renderer.suspend();
    let edited: string | null = null;
    try {
      edited = await editTextInExternalEditor(selectedItem.body || "", "json");
    } finally {
      renderer.resume();
    }

    if (edited == null) {
      setStatusError("External editor exited non-zero.");
      return;
    }

    const maybeFormatted = maybePrettyJsonBody(edited);
    if (!maybeFormatted.ok) {
      setStatusError(`Body JSON invalid: ${maybeFormatted.error}`);
      return;
    }

    try {
      const saved = await saveRequest({
        ...selectedItem,
        body: maybeFormatted.body,
      });
      await reloadRequests(saved.id);
      setStatusInfo(`Body updated for ${saved.name}.`);
    } catch (err) {
      setStatusError(`Body save failed: ${err instanceof Error ? err.message : String(err)}`);
    }
  }

  async function editDraftBodyExternally(): Promise<void> {
    if (!editorDraft) {
      setStatusError("No editor draft for body edit.");
      return;
    }

    renderer.suspend();
    let edited: string | null = null;
    try {
      edited = await editTextInExternalEditor(editorDraft.body || "", "json");
    } finally {
      renderer.resume();
    }

    if (edited == null) {
      setStatusError("External editor exited non-zero.");
      return;
    }

    const maybeFormatted = maybePrettyJsonBody(edited);
    if (!maybeFormatted.ok) {
      setStatusError(`Body JSON invalid: ${maybeFormatted.error}`);
      return;
    }

    setEditorDraft((prev) => {
      if (!prev) {
        return prev;
      }
      return {
        ...prev,
        body: maybeFormatted.body,
      };
    });
    setStatusInfo("Editor body updated from external editor.");
  }

  async function executeMainCommand(raw: string): Promise<void> {
    const line = raw.trim();
    if (!line) {
      setStatusInfo(MAIN_IDLE_HINT);
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

    if (cmd === "export") {
      try {
        const dirArg = parts.slice(1).join(" ").trim();
        const result = await exportRequests(dirArg || undefined);
        setStatusInfo(`Exported ${result.count} request(s) to ${result.directory} (scrubbed ${result.scrubbed} secret ref(s)).`);
      } catch (err) {
        setStatusError(`Export failed: ${err instanceof Error ? err.message : String(err)}`);
      }
      return;
    }

    if (cmd === "import") {
      const dirArg = parts.slice(1).join(" ").trim();
      if (!dirArg) {
        setStatusError("Usage: :import <directory>");
        return;
      }
      try {
        const result = await importRequests(dirArg);
        await reloadRequests();
        setStatusInfo(`Imported ${result.imported} request(s) from ${dirArg}.`);
      } catch (err) {
        setStatusError(`Import failed: ${err instanceof Error ? err.message : String(err)}`);
      }
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
      const at = nowIsoNoMillis();
      const errorMessage = err instanceof Error ? err.message : String(err);

      const preview: ResponsePreview = {
        requestName: req.name,
        requestId: req.id,
        method: req.method,
        url: req.url,
        statusCode: 0,
        durationMs: 0,
        error: errorMessage,
        at,
        body: "",
      };
      setLastResponse(preview);
      setResponseBodyScroll(0);

      const run: RunEntry = {
        id: 0,
        request_id: req.id,
        request_name: req.name,
        method: req.method,
        url: req.url,
        status_code: 0,
        duration_ms: 0,
        error: errorMessage,
        created_at: at,
        request_snapshot: buildRequestSnapshot(req),
        response_body: "",
      };
      await recordRun(run);
      if (screen === "HISTORY") {
        await reloadHistory();
      }

      setStatusError(`Failed to send request: ${errorMessage}`);
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

  function setMainVerticalRatioFromX(x: number): void {
    const left = clamp(Math.round(x), MAIN_MIN_LEFT_COLS, Math.max(MAIN_MIN_LEFT_COLS, termWidth - MAIN_MIN_RIGHT_COLS - 1));
    const ratio = left / Math.max(termWidth, 1);
    setMainSplitRatio((prev) => (Math.abs(prev - ratio) < 0.0001 ? prev : ratio));
  }

  function setMainHorizontalRatioFromY(y: number): void {
    const localRow = clamp(Math.round(y) - 1, 1, contentRows);
    const bottom = clamp(contentRows - localRow, mainBottomMinRows, maxResponseRows);
    const ratio = bottom / Math.max(contentRows, 1);
    setMainResponseRatio((prev) => (Math.abs(prev - ratio) < 0.0001 ? prev : ratio));
  }

  function setHistoryVerticalRatioFromX(x: number): void {
    const left = clamp(
      Math.round(x),
      HISTORY_MIN_LEFT_COLS,
      Math.max(HISTORY_MIN_LEFT_COLS, termWidth - HISTORY_MIN_RIGHT_COLS - 1),
    );
    const ratio = left / Math.max(termWidth, 1);
    setHistorySplitRatio((prev) => (Math.abs(prev - ratio) < 0.0001 ? prev : ratio));
  }

  function setEditorVerticalRatioFromX(x: number): void {
    const left = clamp(
      Math.round(x),
      EDITOR_MIN_LEFT_COLS,
      Math.max(EDITOR_MIN_LEFT_COLS, termWidth - EDITOR_MIN_RIGHT_COLS - 1),
    );
    const ratio = left / Math.max(termWidth, 1);
    setEditorSplitRatio((prev) => (Math.abs(prev - ratio) < 0.0001 ? prev : ratio));
  }

  function applyDrag(modeValue: DragMode, x: number, y: number): void {
    if (termWidth <= 0 || termHeight <= 0) {
      return;
    }

    if (modeValue === "MAIN_VERTICAL") {
      setMainVerticalRatioFromX(x);
      return;
    }
    if (modeValue === "MAIN_HORIZONTAL") {
      setMainHorizontalRatioFromY(y);
      return;
    }
    if (modeValue === "HISTORY_VERTICAL") {
      setHistoryVerticalRatioFromX(x);
      return;
    }
    if (modeValue === "EDITOR_VERTICAL") {
      setEditorVerticalRatioFromX(x);
    }
  }

  function beginDrag(next: DragMode, event?: MouseLikeEvent): void {
    event?.preventDefault?.();
    setDragMode(next);
    if (next === "MAIN_VERTICAL" || next === "MAIN_HORIZONTAL") {
      setStatusInfo("Dragging main pane divider...");
    } else if (next === "HISTORY_VERTICAL") {
      setStatusInfo("Dragging history divider...");
    } else if (next === "EDITOR_VERTICAL") {
      setStatusInfo("Dragging editor divider...");
    }
    if (event && (event.x != null || event.y != null)) {
      const x = event.x ?? 0;
      const y = event.y ?? 0;
      applyDrag(next, x, y);
    }
  }

  function stopDrag(): void {
    const modeValue = dragMode;
    if (modeValue !== "NONE") {
      setDragMode("NONE");
      if (modeValue === "MAIN_VERTICAL" || modeValue === "MAIN_HORIZONTAL") {
        setStatusInfo(`Resize: left=${mainLeftCols} cols response=${responseRows} rows`);
      } else if (modeValue === "HISTORY_VERTICAL") {
        setStatusInfo(`History resize: left=${historyLeftCols} cols`);
      } else if (modeValue === "EDITOR_VERTICAL") {
        setStatusInfo(`Editor resize: left=${editorLeftCols} cols`);
      }
    }
  }

  function handleMouseDrag(event: MouseLikeEvent): void {
    if (dragMode === "NONE") {
      return;
    }
    const x = event.x ?? 0;
    const y = event.y ?? 0;
    applyDrag(dragMode, x, y);
  }

  function handleMouseMove(event: MouseLikeEvent): void {
    if (dragMode === "NONE") {
      return;
    }
    const x = event.x ?? 0;
    const y = event.y ?? 0;
    applyDrag(dragMode, x, y);
  }

  useKeyboard((key) => {
    const ch = printableChar(key);
    const inputChunk = bottomBarInputChunk(key);

    if (screen === "HELP") {
      if (key.name === "escape" || ch === "q") {
        setScreen("MAIN");
        setStatusInfo(MAIN_IDLE_HINT);
      }
      return;
    }

    if (screen === "HISTORY") {
      if (historyMode === "SEARCH") {
        if (key.name === "escape") {
          setHistoryFilter(historySearchSnapshot);
          setHistorySearchInput(historySearchSnapshot);
          setHistoryMode("NORMAL");
          setStatusInfo(HISTORY_IDLE_HINT);
          return;
        }
        if (key.ctrl && key.name === "v") {
          appendClipboardText((chunk) => {
            setHistorySearchInput((prev) => {
              const next = `${prev}${chunk}`;
              setHistoryFilter(next);
              return next;
            });
          });
          return;
        }
        if (key.ctrl && key.name === "y") {
          copyInputToClipboard(historySearchInput);
          return;
        }
        if (clearInputPressed(key)) {
          setHistorySearchInput("");
          setHistoryFilter("");
          return;
        }
        if (key.name === "enter" || key.name === "return") {
          setHistoryMode("NORMAL");
          setStatusInfo(`History filter locked: ${historyFilter || "(none)"}`);
          return;
        }
        if (backspacePressed(key)) {
          setHistorySearchInput((prev) => {
            const next = prev.slice(0, -1);
            setHistoryFilter(next);
            return next;
          });
          return;
        }
        if (inputChunk) {
          setHistorySearchInput((prev) => {
            const next = `${prev}${inputChunk}`;
            setHistoryFilter(next);
            return next;
          });
          return;
        }
        return;
      }

      if (key.name === "escape") {
        setScreen("MAIN");
        setStatusInfo(MAIN_IDLE_HINT);
        return;
      }
      if (ch === "/") {
        setHistorySearchSnapshot(historyFilter);
        setHistorySearchInput(historyFilter);
        setHistoryMode("SEARCH");
        return;
      }
      if (key.name === "down" || ch === "j") {
        setHistorySelected((prev) => clamp(prev + 1, 0, Math.max(0, historyVisible.length - 1)));
        return;
      }
      if (key.name === "up" || ch === "k") {
        setHistorySelected((prev) => clamp(prev - 1, 0, Math.max(0, historyVisible.length - 1)));
        return;
      }
      if (ch === "r") {
        void replaySelectedRun();
        return;
      }
      if (ch === "H") {
        const nextLeft = clamp(historyLeftCols - 1, HISTORY_MIN_LEFT_COLS, historyMaxLeftCols);
        setHistorySplitRatio(nextLeft / Math.max(termWidth, 1));
        setStatusInfo(`History resize: left=${nextLeft} cols`);
        return;
      }
      if (ch === "L") {
        const nextLeft = clamp(historyLeftCols + 1, HISTORY_MIN_LEFT_COLS, historyMaxLeftCols);
        setHistorySplitRatio(nextLeft / Math.max(termWidth, 1));
        setStatusInfo(`History resize: left=${nextLeft} cols`);
        return;
      }
      if (ch === "K") {
        setHistoryDetailRatio((prev) => clamp(prev - 0.04, 0.32, 0.75));
        setStatusInfo("History detail resize: request pane grows");
        return;
      }
      if (ch === "J") {
        setHistoryDetailRatio((prev) => clamp(prev + 0.04, 0.32, 0.75));
        setStatusInfo("History detail resize: response pane grows");
        return;
      }
      if (ch === "{") {
        setHistoryRequestBodyScroll((prev) => Math.max(0, prev - 1));
        return;
      }
      if (ch === "}") {
        setHistoryRequestBodyScroll((prev) => prev + 1);
        return;
      }
      if (ch === "[") {
        setHistoryResponseBodyScroll((prev) => Math.max(0, prev - 1));
        return;
      }
      if (ch === "]") {
        setHistoryResponseBodyScroll((prev) => prev + 1);
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
        if (key.ctrl && key.name === "v") {
          appendClipboardText((chunk) => setEditorInput((prev) => `${prev}${chunk}`));
          return;
        }
        if (key.ctrl && key.name === "y") {
          copyInputToClipboard(editorInput);
          return;
        }
        if (clearInputPressed(key)) {
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
        if (inputChunk) {
          setEditorInput((prev) => `${prev}${inputChunk}`);
        }
        return;
      }

      if (editorMode === "COMMAND") {
        if (key.name === "escape") {
          setEditorMode("NORMAL");
          setEditorCommandInput("");
          return;
        }
        if (key.ctrl && key.name === "v") {
          appendClipboardText((chunk) => setEditorCommandInput((prev) => `${prev}${chunk}`));
          return;
        }
        if (key.ctrl && key.name === "y") {
          copyInputToClipboard(editorCommandInput);
          return;
        }
        if (clearInputPressed(key)) {
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
        if (inputChunk) {
          setEditorCommandInput((prev) => `${prev}${inputChunk}`);
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
        void editDraftBodyExternally();
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
        setStatusInfo(MAIN_IDLE_HINT);
        return;
      }
      if (key.ctrl && key.name === "v") {
        appendClipboardText((chunk) => {
          setSearchInput((prev) => {
            const next = `${prev}${chunk}`;
            setFilter(next);
            return next;
          });
        });
        return;
      }
      if (key.ctrl && key.name === "y") {
        copyInputToClipboard(searchInput);
        return;
      }
      if (clearInputPressed(key)) {
        setSearchInput("");
        setFilter("");
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
      if (inputChunk) {
        setSearchInput((prev) => {
          const next = `${prev}${inputChunk}`;
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
        setStatusInfo(MAIN_IDLE_HINT);
        return;
      }
      if (key.ctrl && key.name === "v") {
        appendClipboardText((chunk) => setCommandInput((prev) => `${prev}${chunk}`));
        return;
      }
      if (key.ctrl && key.name === "y") {
        copyInputToClipboard(commandInput);
        return;
      }
      if (clearInputPressed(key)) {
        setCommandInput("");
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
      if (inputChunk) {
        setCommandInput((prev) => `${prev}${inputChunk}`);
      }
      return;
    }

    if (mode === "ACTION") {
      if (key.name === "escape" || ch === "n") {
        setMode("NORMAL");
        setStatusInfo(MAIN_IDLE_HINT);
        return;
      }
      if (ch === "y") {
        setMode("NORMAL");
        void runSelectedRequest();
        return;
      }
      if (ch === "e") {
        setMode("NORMAL");
        void editSelectedBodyExternally();
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
      setStatusInfo(MAIN_IDLE_HINT);
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
      const nextLeft = clamp(mainLeftCols - 1, MAIN_MIN_LEFT_COLS, mainMaxLeftCols);
      setMainSplitRatio(nextLeft / Math.max(termWidth, 1));
      setStatusInfo(`Resize: left=${nextLeft} cols response=${responseRows} rows`);
      return;
    }
    if (ch === "L") {
      const nextLeft = clamp(mainLeftCols + 1, MAIN_MIN_LEFT_COLS, mainMaxLeftCols);
      setMainSplitRatio(nextLeft / Math.max(termWidth, 1));
      setStatusInfo(`Resize: left=${nextLeft} cols response=${responseRows} rows`);
      return;
    }
    if (ch === "K") {
      const nextRows = clamp(responseRows + 1, mainBottomMinRows, maxResponseRows);
      setMainResponseRatio(nextRows / Math.max(contentRows, 1));
      setStatusInfo(`Resize: left=${mainLeftCols} cols response=${nextRows} rows`);
      return;
    }
    if (ch === "J") {
      const nextRows = clamp(responseRows - 1, mainBottomMinRows, maxResponseRows);
      setMainResponseRatio(nextRows / Math.max(contentRows, 1));
      setStatusInfo(`Resize: left=${mainLeftCols} cols response=${nextRows} rows`);
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
      setSearchPrefix("/");
      setMode("SEARCH");
      return;
    }
    if (ch === "?") {
      setSearchSnapshot(filter);
      setSearchInput(filter);
      setSearchPrefix("?");
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

  const showMainRight = termWidth >= 90;
  const showHistoryRight = termWidth >= 84;
  const showEditorRight = termWidth >= 90;

  const contentRows = Math.max(3, termHeight - 3);
  const mainTopMinRows = Math.min(MAIN_MIN_TOP_ROWS, Math.max(1, contentRows - MAIN_MIN_BOTTOM_ROWS - 1));
  const mainBottomMinRows = Math.min(MAIN_MIN_BOTTOM_ROWS, Math.max(1, contentRows - mainTopMinRows - 1));
  const maxResponseRows = Math.max(mainBottomMinRows, contentRows - mainTopMinRows - 1);
  const responseRows = clamp(Math.round(mainResponseRatio * contentRows), mainBottomMinRows, maxResponseRows);
  const topRows = Math.max(1, contentRows - responseRows - 1);

  const mainMaxLeftCols = Math.max(MAIN_MIN_LEFT_COLS, termWidth - MAIN_MIN_RIGHT_COLS - 1);
  const mainLeftCols = clamp(Math.round(mainSplitRatio * termWidth), MAIN_MIN_LEFT_COLS, mainMaxLeftCols);
  const mainRightCols = Math.max(MAIN_MIN_RIGHT_COLS, termWidth - mainLeftCols - 1);

  const historyMaxLeftCols = Math.max(HISTORY_MIN_LEFT_COLS, termWidth - HISTORY_MIN_RIGHT_COLS - 1);
  const historyLeftCols = clamp(Math.round(historySplitRatio * termWidth), HISTORY_MIN_LEFT_COLS, historyMaxLeftCols);
  const historyRightCols = Math.max(HISTORY_MIN_RIGHT_COLS, termWidth - historyLeftCols - 1);

  const editorMaxLeftCols = Math.max(EDITOR_MIN_LEFT_COLS, termWidth - EDITOR_MIN_RIGHT_COLS - 1);
  const editorLeftCols = clamp(Math.round(editorSplitRatio * termWidth), EDITOR_MIN_LEFT_COLS, editorMaxLeftCols);
  const editorRightCols = Math.max(EDITOR_MIN_RIGHT_COLS, termWidth - editorLeftCols - 1);

  const mainContentCols = Math.max(1, termWidth - 2);
  const historyContentCols = Math.max(1, termWidth - 2);
  const editorContentCols = Math.max(1, termWidth - 2);

  const requestListTextWidth = Math.max(1, (showMainRight ? mainLeftCols : mainContentCols) - 2);
  const requestListNameWidth = Math.max(1, requestListTextWidth - 8);
  const requestPreviewMetaTextWidth = Math.max(1, mainRightCols - 2);
  const requestPreviewBodyTextWidth = Math.max(1, mainRightCols - 6);
  const responsePaneTextWidth = Math.max(1, termWidth - 4);
  const responseBodyTextWidth = Math.max(18, termWidth - 8);
  const historyListTextWidth = Math.max(1, (showHistoryRight ? historyLeftCols : historyContentCols) - 2);
  const historyListNameWidth = Math.max(1, historyListTextWidth - 15);
  const historyDetailMetaTextWidth = Math.max(1, historyRightCols - 4);
  const historyDetailBodyTextWidth = Math.max(12, historyRightCols - 10);
  const historyDetailAvailableRows = Math.max(5, contentRows - 3);
  const historyRequestRows = clamp(
    Math.round(historyDetailRatio * (historyDetailAvailableRows - 1)),
    5,
    Math.max(5, historyDetailAvailableRows - 1 - 4),
  );
  const historyResponseRows = Math.max(4, historyDetailAvailableRows - historyRequestRows - 1);
  const historyRequestBodyRows = Math.max(1, historyRequestRows - 8);
  const historyResponseBodyRows = Math.max(1, historyResponseRows - 8);
  const editorListTextWidth = Math.max(1, (showEditorRight ? editorLeftCols : editorContentCols) - 2);
  const editorMetaTextWidth = Math.max(1, editorRightCols - 2);
  const editorBodyTextWidth = Math.max(12, editorRightCols - 6);
  const helpTextWidth = Math.max(24, termWidth - 4);

  const requestBodyRows = Math.max(1, topRows - 9);
  const responseBodyRows = Math.max(1, responseRows - 11);

  const requestBodySource = selectedItem?.body || "(empty)";
  const requestBodyDisplay = isLikelyJson(requestBodySource)
    ? (() => {
        const pretty = maybePrettyJsonBody(requestBodySource);
        return pretty.ok ? pretty.body : requestBodySource;
      })()
    : requestBodySource;

  const responseBodySource = lastResponse?.body || "";
  const responseBodyDisplay = isLikelyJson(responseBodySource)
    ? (() => {
        const pretty = maybePrettyJsonBody(responseBodySource);
        return pretty.ok ? pretty.body : responseBodySource;
      })()
    : responseBodySource;

  const requestBodyVisibleLines = bodySliceLines(requestBodyDisplay, requestBodyScroll, requestBodyRows, requestPreviewBodyTextWidth);
  const requestBodyVisible = requestBodyVisibleLines.join("\n");
  const responseBodyVisibleLines = bodySliceLines(responseBodyDisplay, responseBodyScroll, responseBodyRows, responseBodyTextWidth);
  const responseBodyVisible = responseBodyVisibleLines.join("\n");
  const parsedHistoryRequest = useMemo(
    () => parseRequestSnapshot(selectedRun?.request_snapshot || ""),
    [selectedRun?.id, selectedRun?.request_snapshot],
  );
  const historyRequestBodyDisplay = isLikelyJson(parsedHistoryRequest.body)
    ? (() => {
        const pretty = maybePrettyJsonBody(parsedHistoryRequest.body);
        return pretty.ok ? pretty.body : parsedHistoryRequest.body;
      })()
    : parsedHistoryRequest.body;
  const historyResponseBodySource = selectedRun?.response_body || "";
  const historyResponseBodyDisplay = isLikelyJson(historyResponseBodySource)
    ? (() => {
        const pretty = maybePrettyJsonBody(historyResponseBodySource);
        return pretty.ok ? pretty.body : historyResponseBodySource;
      })()
    : historyResponseBodySource;
  const historyRequestBodyLines = bodySliceLines(
    historyRequestBodyDisplay,
    historyRequestBodyScroll,
    historyRequestBodyRows,
    historyDetailBodyTextWidth,
  );
  const historyResponseBodyLines = bodySliceLines(
    historyResponseBodyDisplay,
    historyResponseBodyScroll,
    historyResponseBodyRows,
    historyDetailBodyTextWidth,
  );
  const historyRequestBodyVisible = historyRequestBodyLines.join("\n");
  const historyResponseBodyVisible = historyResponseBodyLines.join("\n");
  const editorBodyVisible = bodySlice(editorDraft?.body || "(empty)", editorBodyScroll, 220, editorBodyTextWidth);

  const dividerIdleColor = palette.divider;
  const dividerActiveColor = palette.section;
  const mainVerticalDividerColor = dragMode === "MAIN_VERTICAL" ? dividerActiveColor : dividerIdleColor;
  const mainHorizontalDividerColor = dragMode === "MAIN_HORIZONTAL" ? dividerActiveColor : dividerIdleColor;
  const historyVerticalDividerColor = dragMode === "HISTORY_VERTICAL" ? dividerActiveColor : dividerIdleColor;
  const editorVerticalDividerColor = dragMode === "EDITOR_VERTICAL" ? dividerActiveColor : dividerIdleColor;

  function handleRootMouseDown(event: MouseLikeEvent): void {
    if (dragMode !== "NONE") {
      return;
    }
    const x = event.x ?? 0;
    const y = event.y ?? 0;

    if (screen === "MAIN") {
      if (showMainRight) {
        const dividerX = mainLeftCols + 1;
        if (Math.abs(x - dividerX) <= 1) {
          beginDrag("MAIN_VERTICAL", event);
          return;
        }
      }

      const dividerY = 1 + topRows + 1;
      if (Math.abs(y - dividerY) <= 1) {
        beginDrag("MAIN_HORIZONTAL", event);
        return;
      }
    }

    if (screen === "HISTORY" && showHistoryRight) {
      const dividerX = historyLeftCols + 1;
      if (Math.abs(x - dividerX) <= 1) {
        beginDrag("HISTORY_VERTICAL", event);
        return;
      }
    }

    if (screen === "EDITOR" && showEditorRight) {
      const dividerX = editorLeftCols + 1;
      if (Math.abs(x - dividerX) <= 1) {
        beginDrag("EDITOR_VERTICAL", event);
      }
    }
  }

  function renderMainScreen() {
    const listRows = Math.max(1, topRows - 4);
    const listStart = clamp(selected - Math.floor(listRows / 2), 0, Math.max(0, visible.length - listRows));
    const listWindow = visible.slice(listStart, listStart + listRows);
    const listDisplayCount = visible.length === 0 ? 1 : listWindow.length;
    const listPadRows = Math.max(0, listRows - listDisplayCount);

    const previewNameParts = wrapLabelValue("name:", selectedItem?.name || "(none)", requestPreviewMetaTextWidth, 1);
    const previewMethodParts = wrapLabelValue("method:", selectedItem?.method || "(none)", requestPreviewMetaTextWidth, 1);
    const previewUrlParts = wrapLabelValue("url:", selectedItem?.url || "(none)", requestPreviewMetaTextWidth, 2);
    const previewStatusLine = fitTo(selectedItem ? "" : "No request selected.", requestPreviewMetaTextWidth);
    const previewUrlContinuation = `${" ".repeat(previewUrlParts.labelWidth)}${previewUrlParts.valueLines[1] || " ".repeat(previewUrlParts.valueWidth)}`;
    const previewHasJsonBody = Boolean(selectedItem && isLikelyJson(requestBodyDisplay));

    const responseRequestParts = wrapLabelValue(
      "request:",
      lastResponse ? `${lastResponse.requestName} (${lastResponse.requestId})` : "",
      responsePaneTextWidth,
      1,
    );
    const responseMethodParts = wrapLabelValue("method:", lastResponse?.method || "", responsePaneTextWidth, 1);
    const responseUrlParts = wrapLabelValue("url:", lastResponse?.url || "", responsePaneTextWidth, 2);
    const responseAtParts = wrapLabelValue("at:", lastResponse?.at || "", responsePaneTextWidth, 1);
    const responseStatusParts = wrapLabelValue("status:", lastResponse ? String(lastResponse.statusCode) : "", responsePaneTextWidth, 1);
    const responseMsParts = wrapLabelValue("ms:", lastResponse ? String(lastResponse.durationMs) : "", responsePaneTextWidth, 1);
    const statusValueColor = responseStatusColor(lastResponse?.statusCode);
    const responseErrorParts = wrapLabelValue("error:", lastResponse?.error || "", responsePaneTextWidth, 1);
    const responseUrlContinuation = `${" ".repeat(responseUrlParts.labelWidth)}${responseUrlParts.valueLines[1] || " ".repeat(responseUrlParts.valueWidth)}`;
    const responseEmptyLine = fitTo("No response yet. Select a request, press Enter, then y.", responsePaneTextWidth);

    return (
      <box flexGrow={1} flexDirection="column" border borderColor={palette.border}>
        <box height={topRows} flexDirection="row">
          <box width={showMainRight ? mainLeftCols : "100%"} paddingX={1} paddingTop={1} backgroundColor={palette.panel} flexDirection="column">
            <text fg={palette.section}>Requests</text>
            <text fg={palette.hint}>{fitTo(`Filter: ${filter || "(none)"}`, requestListTextWidth)}</text>
            <box key={`req-list-${visible.length}-${selected}`} flexGrow={1} marginTop={1} backgroundColor={palette.panel} flexDirection="column">
              {visible.length === 0 ? <text fg={palette.warn}>{fitTo("No matching requests.", requestListTextWidth)}</text> : null}
              {listWindow.map((req, localIdx) => {
                const idx = listStart + localIdx;
                const active = idx === selected;
                return (
                  <text key={req.id} fg={palette.hint}>
                    <span fg={active ? palette.selection : palette.hint}>{active ? ">" : " "}</span>
                    <span fg={methodColor(req.method)}>{req.method.padEnd(6, " ")}</span>
                    <span fg={active ? palette.selection : palette.hint}> {fitTo(req.name, requestListNameWidth)}</span>
                  </text>
                );
              })}
              {Array.from({ length: listPadRows }).map((_, idx) => (
                <text key={`main-list-pad-${idx}`} fg={palette.hint}>{fitTo("", requestListTextWidth)}</text>
              ))}
            </box>
          </box>

          {showMainRight ? (
            <box
              width={1}
              border={["left"]}
              borderColor={mainVerticalDividerColor}
              onMouseDown={(event) => beginDrag("MAIN_VERTICAL", event)}
            />
          ) : null}

          {showMainRight ? (
            <box width={mainRightCols} paddingX={1} paddingTop={1} backgroundColor={palette.bg} flexDirection="column">
              <text fg={palette.section}>Request Preview</text>
              <box key={`preview-${selectedItem?.id ?? "none"}`} marginTop={1} flexDirection="column" gap={0}>
                <box height={1} backgroundColor={palette.bg}>
                  <text>
                    <span fg={palette.hint}><strong>{previewNameParts.label}</strong> </span>
                    <span fg={palette.hint}>{previewNameParts.valueLines[0]}</span>
                  </text>
                </box>
                <box height={1} backgroundColor={palette.bg}>
                  <text>
                    <span fg={palette.hint}><strong>{previewMethodParts.label}</strong> </span>
                    <span fg={selectedItem ? methodColor(selectedItem.method) : palette.hint}>{previewMethodParts.valueLines[0]}</span>
                  </text>
                </box>
                <box height={1} backgroundColor={palette.bg}>
                  <text>
                    <span fg={palette.hint}><strong>{previewUrlParts.label}</strong> </span>
                    <span fg={palette.hint}>{previewUrlParts.valueLines[0]}</span>
                  </text>
                </box>
                <box height={1} backgroundColor={palette.bg}>
                  <text fg={palette.hint}>{previewUrlContinuation}</text>
                </box>
                <box height={1} backgroundColor={palette.bg}>
                  <text fg={selectedItem ? palette.hint : palette.warn}>{previewStatusLine}</text>
                </box>
                <text fg={palette.section}>Body</text>
                <BodyPane
                  keyPrefix={`preview-body-${selectedItem?.id ?? "none"}`}
                  lines={requestBodyVisibleLines}
                  visibleText={requestBodyVisible}
                  hasJson={previewHasJsonBody}
                />
              </box>
            </box>
          ) : null}
        </box>

        <box
          height={1}
          border={["top"]}
          borderColor={mainHorizontalDividerColor}
          onMouseDown={(event) => beginDrag("MAIN_HORIZONTAL", event)}
        />

        <box height={responseRows} paddingX={1} paddingTop={1} backgroundColor={palette.bg} flexDirection="column">
          <text fg={palette.section}>Response</text>
          <box flexDirection="column">
            <box height={1} backgroundColor={palette.bg}>
              {lastResponse ? (
                <text>
                  <span fg={palette.hint}><strong>{responseRequestParts.label}</strong> </span>
                  <span fg={palette.hint}>{responseRequestParts.valueLines[0]}</span>
                </text>
              ) : (
                <text fg={palette.hint}>{responseEmptyLine}</text>
              )}
            </box>
            <box height={1} backgroundColor={palette.bg}>
              <text>
                <span fg={palette.hint}><strong>{responseMethodParts.label}</strong> </span>
                <span fg={lastResponse ? methodColor(lastResponse.method) : palette.hint}>{responseMethodParts.valueLines[0]}</span>
              </text>
            </box>
            <box height={1} backgroundColor={palette.bg}>
              <text>
                <span fg={palette.hint}><strong>{responseUrlParts.label}</strong> </span>
                <span fg={palette.hint}>{responseUrlParts.valueLines[0]}</span>
              </text>
            </box>
            <box height={1} backgroundColor={palette.bg}>
              <text fg={palette.hint}>{responseUrlContinuation}</text>
            </box>
            <box height={1} backgroundColor={palette.bg}>
              <text>
                <span fg={palette.hint}><strong>{responseAtParts.label}</strong> </span>
                <span fg={palette.hint}>{responseAtParts.valueLines[0]}</span>
              </text>
            </box>
            <box height={1} backgroundColor={palette.bg}>
              <text>
                <span fg={palette.hint}><strong>{responseStatusParts.label}</strong> </span>
                <span fg={statusValueColor}>{responseStatusParts.valueLines[0]}</span>
              </text>
            </box>
            <box height={1} backgroundColor={palette.bg}>
              <text>
                <span fg={palette.hint}><strong>{responseMsParts.label}</strong> </span>
                <span fg={palette.hint}>{responseMsParts.valueLines[0]}</span>
              </text>
            </box>
            <box height={1} backgroundColor={palette.bg}>
              <text>
                <span fg={lastResponse?.error ? palette.error : palette.hint}><strong>{responseErrorParts.label}</strong> </span>
                <span fg={lastResponse?.error ? palette.error : palette.hint}>{responseErrorParts.valueLines[0]}</span>
              </text>
            </box>
            <text fg={palette.section}>Body</text>
            <box flexGrow={1} marginBottom={1} border borderColor={palette.border} paddingX={1} paddingY={0} backgroundColor={palette.bg}>
              <text fg={palette.hint}>{lastResponse ? responseBodyVisible || "(empty)" : "(empty)"}</text>
            </box>
          </box>
        </box>
      </box>
    );
  }

  function renderHistoryScreen() {
    const requestRunAtParts = wrapLabelValue(
      "run/at:",
      selectedRun ? `#${selectedRun.id} ${selectedRun.created_at}` : "",
      historyDetailMetaTextWidth,
      1,
    );
    const requestNameParts = wrapLabelValue("name:", selectedRun?.request_name || "(none)", historyDetailMetaTextWidth, 1);
    const requestMethodParts = wrapLabelValue("method:", selectedRun?.method || "(none)", historyDetailMetaTextWidth, 1);
    const requestUrlParts = wrapLabelValue("url:", selectedRun?.url || "(none)", historyDetailMetaTextWidth, 2);
    const requestUrlContinuation = `${" ".repeat(requestUrlParts.labelWidth)}${requestUrlParts.valueLines[1] || " ".repeat(requestUrlParts.valueWidth)}`;
    const requestAuthHeaderParts = wrapLabelValue(
      "auth/header:",
      `${parsedHistoryRequest.fields.auth || "none"}; ${parsedHistoryRequest.fields.header || "none"}`,
      historyDetailMetaTextWidth,
      1,
    );

    const responseRequestParts = wrapLabelValue(
      "request:",
      selectedRun ? `${selectedRun.request_name} (${selectedRun.request_id})` : "",
      historyDetailMetaTextWidth,
      1,
    );
    const responseMethodParts = wrapLabelValue("method:", selectedRun?.method || "", historyDetailMetaTextWidth, 1);
    const responseStatusParts = wrapLabelValue("status:", selectedRun ? historyStatusLabel(selectedRun) : "", historyDetailMetaTextWidth, 1);
    const responseMsParts = wrapLabelValue("ms:", selectedRun ? String(selectedRun.duration_ms || 0) : "", historyDetailMetaTextWidth, 1);
    const responseErrorParts = wrapLabelValue("error:", selectedRun?.error || "", historyDetailMetaTextWidth, 2);
    const responseErrorContinuation = `${" ".repeat(responseErrorParts.labelWidth)}${responseErrorParts.valueLines[1] || " ".repeat(responseErrorParts.valueWidth)}`;
    const responseStatusValueColor = selectedRun ? historyStatusColor(selectedRun) : palette.hint;

    const requestBodyHasJson = isLikelyJson(historyRequestBodyDisplay);
    const responseBodyHasJson = isLikelyJson(historyResponseBodyDisplay);

    return (
      <box flexGrow={1} border borderColor={palette.border} flexDirection="row">
        <box width={showHistoryRight ? historyLeftCols : "100%"} paddingX={1} paddingTop={1} backgroundColor={palette.panel} flexDirection="column">
          <text fg={palette.section}>History</text>
          <text fg={palette.hint}>{fitTo(`Filter${historyMode === "SEARCH" ? " (/):" : ":"} ${historyFilter || "(none)"}`, historyListTextWidth)}</text>
          <scrollbox flexGrow={1} marginTop={1} backgroundColor={palette.panel}>
            {historyVisible.map((run, idx) => {
              const active = idx === historySelected;
              const statusColor = historyStatusColor(run);
              const statusBadge = fitTo(historyStatusLabel(run), 5);
              return (
                <text key={`${run.id}`} fg={palette.hint}>
                  <span fg={active ? palette.selection : palette.hint}>{active ? ">" : " "}</span>
                  <span fg={methodColor(run.method)}>{run.method.padEnd(6, " ")}</span>
                  <span fg={active ? palette.selection : palette.hint}> {fitTo(run.request_name || run.request_id, historyListNameWidth)}</span>
                  <span fg={statusColor}> {statusBadge}</span>
                </text>
              );
            })}
            {historyVisible.length === 0 ? <text fg={palette.warn}>No matching history runs.</text> : null}
          </scrollbox>
        </box>

        {showHistoryRight ? <box width={1} border={["left"]} borderColor={historyVerticalDividerColor} onMouseDown={(event) => beginDrag("HISTORY_VERTICAL", event)} /> : null}

        {showHistoryRight ? (
          <box width={historyRightCols} paddingX={1} paddingTop={1} backgroundColor={palette.bg} flexDirection="column">
            <text fg={palette.section}>Run Detail</text>
            {selectedRun ? (
              <box marginTop={1} flexDirection="column">
                <box height={historyRequestRows} border borderColor={palette.border} paddingX={1} backgroundColor={palette.bg} flexDirection="column">
                  <text fg={palette.section}>Request</text>
                  <box height={1}><text><span fg={palette.hint}><strong>{requestRunAtParts.label}</strong> </span><span fg={palette.hint}>{requestRunAtParts.valueLines[0]}</span></text></box>
                  <box height={1}><text><span fg={palette.hint}><strong>{requestNameParts.label}</strong> </span><span fg={palette.hint}>{requestNameParts.valueLines[0]}</span></text></box>
                  <box height={1}><text><span fg={palette.hint}><strong>{requestMethodParts.label}</strong> </span><span fg={methodColor(selectedRun.method)}>{requestMethodParts.valueLines[0]}</span></text></box>
                  <box height={1}><text><span fg={palette.hint}><strong>{requestUrlParts.label}</strong> </span><span fg={palette.hint}>{requestUrlParts.valueLines[0]}</span></text></box>
                  <box height={1}><text fg={palette.hint}>{requestUrlContinuation}</text></box>
                  <box height={1}><text><span fg={palette.hint}><strong>{requestAuthHeaderParts.label}</strong> </span><span fg={palette.hint}>{requestAuthHeaderParts.valueLines[0]}</span></text></box>
                  <text fg={palette.section}>Body</text>
                  <BodyPane
                    keyPrefix={`history-req-body-${selectedRun.id}`}
                    lines={historyRequestBodyLines}
                    visibleText={historyRequestBodyVisible}
                    hasJson={requestBodyHasJson}
                  />
                </box>

                <box height={1} border={['top']} borderColor={palette.divider} />

                <box height={historyResponseRows} border borderColor={palette.border} paddingX={1} backgroundColor={palette.bg} flexDirection="column">
                  <text fg={palette.section}>Response</text>
                  <box height={1}><text><span fg={palette.hint}><strong>{responseRequestParts.label}</strong> </span><span fg={palette.hint}>{responseRequestParts.valueLines[0]}</span></text></box>
                  <box height={1}><text><span fg={palette.hint}><strong>{responseMethodParts.label}</strong> </span><span fg={methodColor(selectedRun.method)}>{responseMethodParts.valueLines[0]}</span></text></box>
                  <box height={1}><text><span fg={palette.hint}><strong>{responseStatusParts.label}</strong> </span><span fg={responseStatusValueColor}>{responseStatusParts.valueLines[0]}</span></text></box>
                  <box height={1}><text><span fg={palette.hint}><strong>{responseMsParts.label}</strong> </span><span fg={palette.hint}>{responseMsParts.valueLines[0]}</span></text></box>
                  <box height={1}><text><span fg={selectedRun.error ? palette.error : palette.hint}><strong>{responseErrorParts.label}</strong> </span><span fg={selectedRun.error ? palette.error : palette.hint}>{responseErrorParts.valueLines[0]}</span></text></box>
                  <box height={1}><text fg={selectedRun.error ? palette.error : palette.hint}>{responseErrorContinuation}</text></box>
                  <text fg={palette.section}>Body</text>
                  <BodyPane
                    keyPrefix={`history-res-body-${selectedRun.id}`}
                    lines={historyResponseBodyLines}
                    visibleText={historyResponseBodyVisible}
                    hasJson={responseBodyHasJson}
                    emptyText="(empty)"
                  />
                </box>
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
        <box width={showEditorRight ? editorLeftCols : "100%"} paddingX={1} paddingTop={1} backgroundColor={palette.panel} flexDirection="column">
          <text fg={palette.section}>Editor</text>
          {draft ? (
            <scrollbox flexGrow={1} marginTop={1} backgroundColor={palette.panel}>
              {EDITOR_FIELDS.map((field, idx) => {
                const active = idx === editorField;
                const value = requestFieldValue(draft, field.key);
                const labelPrefix = `${active ? "> " : "  "}${field.label}: `;
                const fieldValueWidth = Math.max(1, editorListTextWidth - labelPrefix.length);
                return (
                  <text key={field.key} fg={active ? palette.ok : palette.hint}>
                    <span fg={active ? palette.ok : palette.section}>{active ? "> " : "  "}{field.label}: </span>
                    <span fg={field.key === "method" ? methodColor(value) : active ? palette.ok : palette.hint}>
                      {fitTo(value || "(empty)", fieldValueWidth)}
                    </span>
                  </text>
                );
              })}
            </scrollbox>
          ) : (
            <text fg={palette.warn}>No draft.</text>
          )}
        </box>

        {showEditorRight ? <box width={1} border={["left"]} borderColor={editorVerticalDividerColor} onMouseDown={(event) => beginDrag("EDITOR_VERTICAL", event)} /> : null}

        {showEditorRight && draft ? (
          <box width={editorRightCols} paddingX={1} paddingTop={1} backgroundColor={palette.bg} flexDirection="column">
            <text fg={palette.section}>Preview</text>
            <box marginTop={1} flexDirection="column">
              <text fg={palette.hint}>{fitTo(`name: ${draft.name || "(empty)"}`, editorMetaTextWidth)}</text>
              <text fg={methodColor(draft.method)}>{fitTo(`method: ${draft.method}`, editorMetaTextWidth)}</text>
              <text fg={palette.hint}>{fitTo(`url: ${draft.url || "(empty)"}`, editorMetaTextWidth)}</text>
              <text fg={palette.section}>Body</text>
              <scrollbox flexGrow={1} border borderColor={palette.border} paddingX={1} paddingY={0} backgroundColor={palette.bg}>
                <text fg={palette.hint}>{editorBodyVisible}</text>
              </scrollbox>
            </box>
            <text fg={palette.hint}>{fitTo(`Field: ${currentField.label}`, editorMetaTextWidth)}</text>
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
          <text fg={palette.hint}>{trimTo(`  j/k, gg/G, Enter, /, ?, :, d, E, ZZ/ZQ, H/L, K/J, { }, [ ]`, helpTextWidth)}</text>
          <text fg={palette.hint}>{trimTo(`Action: y send, e body edit, a auth editor, Esc/n cancel`, helpTextWidth)}</text>
          <text fg={palette.hint}>{trimTo(`History: j/k, / filter, r replay, H/L split, K/J detail, { } req, [ ] resp, Esc`, helpTextWidth)}</text>
          <text fg={palette.hint}>{trimTo(`Editor: j/k, h/l method, i/Enter edit, :w/:q/:wq, Ctrl+s, Esc`, helpTextWidth)}</text>
          <text fg={palette.hint}>{trimTo(`Bottom input: Ctrl+v paste, Ctrl+y copy, Ctrl+Backspace/Delete clear`, helpTextWidth)}</text>
          <text fg={palette.hint}>{trimTo(`Press Esc to return.`, helpTextWidth)}</text>
        </scrollbox>
      </box>
    );
  }

  let prompt = MAIN_IDLE_HINT;
  let idleHint = MAIN_IDLE_HINT;

  if (screen === "MAIN") {
    prompt = mainPrompt(mode, commandInput, searchInput, searchPrefix, selectedItem?.name ?? "");
    idleHint = MAIN_IDLE_HINT;
  } else if (screen === "HISTORY") {
    prompt = historyMode === "SEARCH" ? `/${historySearchInput}` : HISTORY_IDLE_HINT;
    idleHint = HISTORY_IDLE_HINT;
  } else if (screen === "EDITOR") {
    const currentLabel = EDITOR_FIELDS[editorField]?.label ?? "Field";
    prompt = editorPrompt(editorMode, editorInput, editorCommandInput, currentLabel);
    idleHint = EDITOR_IDLE_HINT;
  } else if (screen === "HELP") {
    prompt = "Esc to return to main";
    idleHint = "Esc to return to main";
  }

  const showPrompt =
    (screen === "MAIN" && mode !== "NORMAL") ||
    (screen === "HISTORY" && historyMode !== "NORMAL") ||
    (screen === "EDITOR" && editorMode !== "NORMAL");

  let bottomBarText = idleHint;
  if (showPrompt) {
    bottomBarText = screen === "EDITOR" ? `[EDITOR ${editorMode}] ${prompt}` : prompt;
  } else if (status && status !== MAIN_IDLE_HINT && status !== HISTORY_IDLE_HINT && status !== EDITOR_IDLE_HINT) {
    bottomBarText = screen === "EDITOR" ? `[EDITOR] ${status}` : status;
  } else if (screen === "EDITOR") {
    bottomBarText = `[EDITOR] ${idleHint}`;
  }

  return (
    <box
      width="100%"
      height="100%"
      backgroundColor={palette.bg}
      flexDirection="column"
      onMouseDown={handleRootMouseDown}
      onMouseDrag={handleMouseDrag}
      onMouseMove={handleMouseMove}
      onMouseUp={stopDrag}
      onMouseDragEnd={stopDrag}
      onMouseDrop={stopDrag}
    >
      <box height={1} paddingX={1} backgroundColor={palette.panelSoft}>
        <text fg={palette.section}>
          <strong>tuiman</strong>
        </text>
      </box>

      {screen === "MAIN" ? renderMainScreen() : null}
      {screen === "HISTORY" ? renderHistoryScreen() : null}
      {screen === "EDITOR" ? renderEditorScreen() : null}
      {screen === "HELP" ? renderHelpScreen() : null}

      <box height={1} paddingX={1} backgroundColor={palette.panelSoft}>
        <text fg={statusIsError ? palette.error : palette.hint}>{fitTo(bottomBarText, Math.max(1, termWidth - 2))}</text>
      </box>
    </box>
  );
}
