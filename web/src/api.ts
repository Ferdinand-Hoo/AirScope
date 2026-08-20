import type {
  ApConfig,
  Capabilities,
  Client,
  RuntimeEvent,
  Status,
  TokenSummary,
} from "./types";

let csrfToken = "";
let onUnauthorized: (() => void) | undefined;

export class ApiError extends Error {
  constructor(
    message: string,
    readonly status: number,
    readonly field?: string,
  ) {
    super(message);
  }
}

export function setUnauthorizedHandler(handler: () => void) {
  onUnauthorized = handler;
}

async function request<T>(path: string, init: RequestInit = {}): Promise<T> {
  const headers = new Headers(init.headers);
  if (init.body) headers.set("Content-Type", "application/json");
  if (init.method && init.method !== "GET" && csrfToken) {
    headers.set("X-CSRF-Token", csrfToken);
  }
  const response = await fetch(path, { ...init, headers, credentials: "same-origin" });
  if (response.status === 401) {
    csrfToken = "";
    onUnauthorized?.();
  }
  if (!response.ok) {
    const body = await response.json().catch(() => null);
    throw new ApiError(
      body?.error?.message ?? `Request failed (${response.status})`,
      response.status,
      body?.error?.field,
    );
  }
  if (response.status === 204 || response.headers.get("content-length") === "0") {
    return undefined as T;
  }
  return response.json() as Promise<T>;
}

export async function login(password: string) {
  const result = await request<{ csrfToken: string }>("/api/v1/session", {
    method: "POST",
    body: JSON.stringify({ password }),
  });
  csrfToken = result.csrfToken;
}

export async function logout() {
  await request<void>("/api/v1/session", { method: "DELETE" });
  csrfToken = "";
}

export const getStatus = () => request<Status>("/api/v1/status");
export const getCapabilities = () =>
  request<Capabilities>("/api/v1/capabilities");
export const getConfig = () => request<ApConfig>("/api/v1/config");
export const getClients = () =>
  request<{ clients: Client[]; count: number }>("/api/v1/clients");
export const getEvents = () =>
  request<{ events: RuntimeEvent[]; count: number }>("/api/v1/events");
export const getTokens = () =>
  request<{ tokens: TokenSummary[] }>("/api/v1/tokens");

export const saveConfig = (
  config: Omit<ApConfig, "apPasswordConfigured">,
) =>
  request<{
    accepted: boolean;
    reconnectRequired: boolean;
    applyDelayMs: number;
  }>("/api/v1/config", {
    method: "PUT",
    body: JSON.stringify(config),
  });

export const switchChannel = (config: ApConfig) =>
  request("/api/v1/channel-switch", {
    method: "POST",
    body: JSON.stringify({
      primaryChannel: config.primaryChannel,
      bandwidth: config.bandwidth,
      secondaryChannel: config.secondaryChannel,
      csaCount: config.csaCount,
    }),
  });

export const createToken = (label: string) =>
  request<{ id: string; label: string; token: string }>("/api/v1/tokens", {
    method: "POST",
    body: JSON.stringify({ label }),
  });

export const revokeToken = (id: string) =>
  request<void>(`/api/v1/tokens/${encodeURIComponent(id)}`, {
    method: "DELETE",
  });

export const rotateCredential = (password: string) =>
  request<void>("/api/v1/credential", {
    method: "PUT",
    body: JSON.stringify({ password }),
  });
