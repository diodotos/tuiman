import { mkdirSync } from "node:fs";
import { join } from "node:path";

export type AppPaths = {
  configDir: string;
  stateDir: string;
  cacheDir: string;
  requestsDir: string;
  historyDbPath: string;
};

export function getPaths(): AppPaths {
  const home = process.env.HOME;
  if (!home) {
    throw new Error("$HOME is not set");
  }

  const configDir = join(home, ".config", "tuiman");
  const stateDir = join(home, ".local", "state", "tuiman");
  const cacheDir = join(home, ".cache", "tuiman");
  const requestsDir = join(configDir, "requests");
  const historyDbPath = join(stateDir, "history.db");

  return {
    configDir,
    stateDir,
    cacheDir,
    requestsDir,
    historyDbPath,
  };
}

export function ensurePaths(paths = getPaths()): AppPaths {
  mkdirSync(paths.configDir, { recursive: true });
  mkdirSync(paths.stateDir, { recursive: true });
  mkdirSync(paths.cacheDir, { recursive: true });
  mkdirSync(paths.requestsDir, { recursive: true });
  return paths;
}
