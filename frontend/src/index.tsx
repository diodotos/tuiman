import { ConsolePosition, createCliRenderer } from "@opentui/core";
import { createRoot } from "@opentui/react";

import { App } from "./App";

const renderer = await createCliRenderer({
  exitOnCtrlC: true,
  useMouse: true,
  consoleOptions: {
    position: ConsolePosition.BOTTOM,
    sizePercent: 25,
    startInDebugMode: false,
  },
});

createRoot(renderer).render(<App />);
