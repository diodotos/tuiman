import { useEffect, useMemo, useState } from "react";
import { useKeyboard, useRenderer } from "@opentui/react";

import { bootstrap } from "./rpc/client";
import { methodColor, palette } from "./theme";
import type { RequestItem } from "./types";

const statusHint =
  "j/k move | / search | : command | Enter actions | E edit | d delete | ZZ/ZQ quit | { } req body | [ ] resp body | drag";

function trimTo(text: string, width: number): string {
  if (text.length <= width) {
    return text;
  }
  return `${text.slice(0, Math.max(0, width - 1))}…`;
}

export function App() {
  const renderer = useRenderer();
  const [requests, setRequests] = useState<RequestItem[]>([]);
  const [selected, setSelected] = useState(0);
  const [filter, setFilter] = useState("");
  const [status, setStatus] = useState(statusHint);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    bootstrap()
      .then((data) => {
        setRequests(data.requests ?? []);
        setStatus(`Loaded ${data.requests?.length ?? 0} request(s) from backend.`);
      })
      .catch((err) => {
        setError(err instanceof Error ? err.message : String(err));
        setStatus("Backend bootstrap failed; running in UI-only mode.");
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

  useKeyboard((key) => {
    if (key.name === "escape") {
      setFilter("");
      setStatus(statusHint);
      return;
    }

    if (key.name === "z") {
      if (key.shift) {
        renderer.destroy();
      }
      return;
    }

    if (key.name === "q" && !key.ctrl && !key.meta && !key.option) {
      renderer.destroy();
      return;
    }

    if (key.name === "j" || key.name === "down") {
      setSelected((prev) => Math.min(prev + 1, Math.max(0, visible.length - 1)));
      return;
    }

    if (key.name === "k" || key.name === "up") {
      setSelected((prev) => Math.max(prev - 1, 0));
      return;
    }

    if (key.name === "/") {
      setStatus("Search placeholder: frontend filtering will be wired to prompt mode next.");
      return;
    }

    if (key.name === "enter") {
      setStatus("Action row placeholder: send/edit/auth actions not wired yet.");
      return;
    }
  });

  return (
    <box width="100%" height="100%" backgroundColor={palette.bg} flexDirection="column">
      <box height={1} paddingX={1} backgroundColor={palette.panelSoft}>
        <text fg={palette.section}>
          <strong>tuiman</strong>
          <span fg={palette.hint}>  OpenTUI frontend + Rust backend scaffold</span>
        </text>
      </box>

      <box flexGrow={1} flexDirection="row" border borderColor={palette.border}>
        <box width="36%" paddingX={1} paddingTop={1} backgroundColor={palette.panel}>
          <text fg={palette.section}>Requests</text>
          <box height={1}>
            <text fg={palette.hint}>{trimTo(`Filter: ${filter || "(none)"}`, 42)}</text>
          </box>
          <scrollbox flexGrow={1} marginTop={1} focused>
            {visible.map((req, idx) => {
              const active = idx === selected;
              return (
                <text key={req.id} fg={active ? palette.ok : palette.hint}>
                  <span fg={active ? palette.ok : methodColor(req.method)}>{req.method.padEnd(6, " ")}</span>
                  <span> {trimTo(req.name, 24)}</span>
                </text>
              );
            })}
            {visible.length === 0 ? <text fg={palette.warn}>No matching requests.</text> : null}
          </scrollbox>
        </box>

        <box width="64%" border borderColor={palette.border} paddingX={1} paddingTop={1}>
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
              <scrollbox height="60%" border borderColor={palette.border} paddingX={1} paddingY={0}>
                <text fg={palette.hint}>{selectedItem.body || "(empty)"}</text>
              </scrollbox>
            </box>
          ) : (
            <box marginTop={1}>
              <text fg={palette.warn}>No request selected.</text>
            </box>
          )}
        </box>
      </box>

      <box border borderColor={palette.border} paddingX={1} height={5}>
        <box flexDirection="column">
          <text fg={palette.section}>Response</text>
          <text fg={palette.hint}>No response yet. Send flow wiring is next.</text>
          <text fg={error ? palette.error : palette.hint}>{trimTo(error ?? status, 140)}</text>
        </box>
      </box>
    </box>
  );
}
