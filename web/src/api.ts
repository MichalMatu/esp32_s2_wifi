export type ApiSource = "device" | "mock";

export type StatusResponse = {
  source: ApiSource;
  system: Record<"runtime" | "cpu" | "reset" | "idf" | "build", string>;
  memory: Record<"freeHeap" | "minFreeHeap" | "flashChip" | "appPartition", string>;
  coredump: {
    enabled: boolean;
    present: boolean;
    size: string;
    error: string;
  };
  network: Record<"mode" | "usb" | "configAccess" | "host", string>;
  config: {
    activeMode: string;
    savedMode: string;
  };
  connection: WifiConnectionStatus;
  led: {
    mode: string;
    state: string;
  };
};

export type ScanNetwork = {
  ssid: string;
  rssi: number;
  security: string;
  channel: number;
};

export type WifiProfile = {
  id: number;
  ssid: string;
  password: string;
};

export type ScanResponse = {
  source: ApiSource;
  ok: boolean;
  state: "idle" | "running" | "done" | "error";
  total: number;
  durationMs: number;
  networks: ScanNetwork[];
  error?: string;
};

export type ProfilesResponse = {
  source: ApiSource;
  ok: boolean;
  profiles: WifiProfile[];
  error?: string;
};

export type WifiConnectionStatus = {
  ok: boolean;
  state: "idle" | "running" | "succeeded" | "failed";
  ssid: string;
  message: string;
  reason: string;
  ip: string;
  rssi: number | null;
  channel: number | null;
  elapsedMs: number;
  bridgePending: boolean;
};

export type ConnectResponse = {
  source: ApiSource;
  connection: WifiConnectionStatus;
};

export type ActionResponse = {
  ok: boolean;
  message: string;
};

export type ApiClient = {
  getStatus(): Promise<StatusResponse>;
  startScan(): Promise<ScanResponse>;
  getScan(): Promise<ScanResponse>;
  getProfiles(): Promise<ProfilesResponse>;
  getConnection(): Promise<ConnectResponse>;
  saveWifi(ssid: string, password: string): Promise<ActionResponse>;
  testWifi(ssid: string, password: string): Promise<ConnectResponse>;
  connectWifi(ssid: string, password: string): Promise<ConnectResponse>;
  connectProfile(id: number): Promise<ConnectResponse>;
  deleteProfile(id: number): Promise<ActionResponse>;
  setConfigMode(mode: string): Promise<ActionResponse>;
  setLedMode(mode: string): Promise<ActionResponse>;
  eraseCoreDump(): Promise<ActionResponse>;
};

const REQUEST_TIMEOUT_MS = 8000;

const apiFetch = async <T>(
  url: string,
  init: RequestInit = {},
  timeoutMs: number | null = REQUEST_TIMEOUT_MS,
): Promise<T> => {
  const controller = timeoutMs === null ? null : new AbortController();
  const timeout =
    timeoutMs === null ? null : window.setTimeout(() => controller?.abort(), timeoutMs);

  let response: Response;
  try {
    response = await fetch(url, { ...init, cache: "no-store", signal: controller?.signal });
  } catch (error) {
    if (error instanceof DOMException && error.name === "AbortError") {
      throw new Error("request timed out");
    }
    throw error;
  } finally {
    if (timeout !== null) {
      window.clearTimeout(timeout);
    }
  }

  if (!response.ok) {
    throw new Error(`HTTP ${response.status}`);
  }
  return (await response.json()) as T;
};

const apiGet = <T>(url: string, timeoutMs?: number | null): Promise<T> =>
  apiFetch<T>(url, undefined, timeoutMs);

const apiForm = <T>(url: string, data: Record<string, string>): Promise<T> =>
  apiFetch<T>(url, {
    method: "POST",
    headers: {
      "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8",
    },
    body: new URLSearchParams(data),
  });

export const httpApi: ApiClient = {
  getStatus: () => apiGet<StatusResponse>("/api/status"),
  startScan: () => apiForm<ScanResponse>("/api/wifi/scan", {}),
  getScan: () => apiGet<ScanResponse>("/api/wifi/scan"),
  getProfiles: () => apiGet<ProfilesResponse>("/api/wifi/profiles"),
  getConnection: () => apiGet<ConnectResponse>("/api/wifi/connection"),
  saveWifi: (ssid, password) => apiForm<ActionResponse>("/api/wifi/save", { ssid, password }),
  testWifi: (ssid, password) => apiForm<ConnectResponse>("/api/wifi/test", { ssid, password }),
  connectWifi: (ssid, password) => apiForm<ConnectResponse>("/api/wifi/reconnect", { ssid, password }),
  connectProfile: (id) => apiForm<ConnectResponse>("/api/wifi/profile/connect", { id: String(id) }),
  deleteProfile: (id) => apiForm<ActionResponse>("/api/wifi/profile/delete", { id: String(id) }),
  setConfigMode: (mode) => apiForm<ActionResponse>("/api/config", { mode }),
  setLedMode: (mode) => apiForm<ActionResponse>("/api/led", { mode }),
  eraseCoreDump: () => apiForm<ActionResponse>("/api/coredump/erase", {}),
};
