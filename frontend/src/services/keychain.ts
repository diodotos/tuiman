const SERVICE = "tuiman";

export async function getSecret(secretRef: string): Promise<string | null> {
  if (!secretRef.trim()) {
    return null;
  }

  const proc = Bun.spawn([
    "/usr/bin/security",
    "find-generic-password",
    "-a",
    secretRef,
    "-s",
    SERVICE,
    "-w",
  ]);

  const stdout = await new Response(proc.stdout).text();
  const code = await proc.exited;
  if (code !== 0) {
    return null;
  }
  return stdout.trim();
}

export async function setSecret(secretRef: string, value: string): Promise<void> {
  if (!secretRef.trim()) {
    throw new Error("secret ref is required");
  }

  const proc = Bun.spawn([
    "/usr/bin/security",
    "add-generic-password",
    "-a",
    secretRef,
    "-s",
    SERVICE,
    "-w",
    value,
    "-U",
  ]);

  const code = await proc.exited;
  if (code !== 0) {
    throw new Error(`failed to store keychain secret for ${secretRef}`);
  }
}
