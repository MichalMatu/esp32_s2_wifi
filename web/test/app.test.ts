import { beforeEach, describe, expect, it, vi } from "vitest";
import html from "../index.html?raw";
import { createApp } from "../src/app";
import type { ApiClient, StatusResponse } from "../src/api";

const bodyHtml = html.match(/<body>([\s\S]*)<\/body>/)?.[1] ?? "";

const status = (overrides: Partial<StatusResponse> = {}): StatusResponse => ({
  source: "mock",
  system: {
    runtime: "1m 56s",
    cpu: "160 MHz",
    reset: "software",
    idf: "5.5.4",
    build: "Jun 26 2026 05:02:10",
  },
  memory: {
    freeHeap: "1.92 MB",
    minFreeHeap: "1.91 MB",
    flashChip: "4.00 MB",
    appPartition: "1.50 MB",
  },
  coredump: {
    enabled: true,
    present: false,
    size: "0 B",
    error: "",
  },
  network: {
    mode: "configuration",
    usb: "NCM config device",
    configAccess: "Captive portal",
    host: "wifi.local / wifi.settings / 192.168.4.1",
  },
  config: {
    activeMode: "captive",
    savedMode: "captive",
  },
  connection: {
    ok: true,
    state: "idle",
    ssid: "",
    message: "No Wi-Fi connection test has run yet.",
    reason: "",
    ip: "",
    rssi: null,
    channel: null,
    elapsedMs: 0,
    bridgePending: false,
  },
  led: {
    mode: "status",
    state: "configuration",
  },
  ...overrides,
});

const makeApi = (): ApiClient => ({
  getStatus: vi.fn().mockResolvedValue(status()),
  startScan: vi.fn().mockResolvedValue({
    ok: true,
    source: "mock",
    state: "done",
    total: 2,
    durationMs: 1200,
    networks: [
      { ssid: "Mock_Home_2G", rssi: -44, security: "WPA2", channel: 6 },
      { ssid: "Mock_Workshop", rssi: -70, security: "WPA2/WPA3", channel: 11 },
    ],
  }),
  getScan: vi.fn().mockResolvedValue({
    ok: true,
    source: "mock",
    state: "done",
    total: 2,
    durationMs: 1200,
    networks: [
      { ssid: "Mock_Home_2G", rssi: -44, security: "WPA2", channel: 6 },
      { ssid: "Mock_Workshop", rssi: -70, security: "WPA2/WPA3", channel: 11 },
    ],
  }),
  getProfiles: vi.fn().mockResolvedValue({
    ok: true,
    source: "mock",
    profiles: [
      { id: 0, ssid: "Mock_Home_2G", password: "mock-home-pass" },
      { id: 1, ssid: "Mock_Workshop", password: "mock-workshop-pass" },
    ],
  }),
  getConnection: vi.fn().mockResolvedValue({
    source: "mock",
    connection: {
      ok: true,
      state: "succeeded",
      ssid: "Mock_Home_2G",
      message: "Wi-Fi validated. Restarting into bridge mode.",
      reason: "",
      ip: "192.168.0.25",
      rssi: -48,
      channel: 6,
      elapsedMs: 1200,
      bridgePending: true,
    },
  }),
  saveWifi: vi.fn().mockResolvedValue({ ok: true, message: "Wi-Fi credentials saved." }),
  testWifi: vi.fn().mockResolvedValue({
    source: "mock",
    connection: {
      ok: true,
      state: "succeeded",
      ssid: "Mock_Home_2G",
      message: "Wi-Fi test passed. Credentials were not saved.",
      reason: "",
      ip: "192.168.0.25",
      rssi: -48,
      channel: 6,
      elapsedMs: 1200,
      bridgePending: false,
    },
  }),
  connectWifi: vi.fn().mockResolvedValue({
    source: "mock",
    connection: {
      ok: true,
      state: "succeeded",
      ssid: "Mock_Home_2G",
      message: "Wi-Fi validated. Restarting into bridge mode.",
      reason: "",
      ip: "192.168.0.25",
      rssi: -48,
      channel: 6,
      elapsedMs: 1200,
      bridgePending: true,
    },
  }),
  connectProfile: vi.fn().mockResolvedValue({
    source: "mock",
    connection: {
      ok: true,
      state: "succeeded",
      ssid: "Mock_Home_2G",
      message: "Wi-Fi validated. Restarting into bridge mode.",
      reason: "",
      ip: "192.168.0.25",
      rssi: -48,
      channel: 6,
      elapsedMs: 1200,
      bridgePending: true,
    },
  }),
  deleteProfile: vi.fn().mockResolvedValue({ ok: true, message: "Saved profile removed." }),
  setConfigMode: vi.fn().mockResolvedValue({ ok: true, message: "Config access mode saved." }),
  setLedMode: vi.fn().mockResolvedValue({ ok: true, message: "LED mode updated." }),
  eraseCoreDump: vi.fn().mockResolvedValue({ ok: true, message: "Core dump erased." }),
});

describe("config UI regression", () => {
  beforeEach(() => {
    document.body.innerHTML = bodyHtml;
  });

  it("keeps the core sections visible after initialization", async () => {
    const api = makeApi();
    await createApp(document, api).init();

    expect(api.startScan).not.toHaveBeenCalled();
    expect(document.body.textContent).toContain("Wi-Fi credentials");
    expect(document.body.textContent).toContain("Wi-Fi scan");
    expect(document.body.textContent).toContain("Connect and restart");
    expect(document.body.textContent).toContain("USB config access");
    expect(document.body.textContent).toContain("Status LED");
    expect(document.body.textContent).toContain("Saved networks");
    expect(document.body.textContent).toContain("System");
    expect(document.body.textContent).toContain("Memory");
    expect(document.body.textContent).toContain("Core dump");
    expect(document.body.textContent).toContain("Network");
  });

  it("switches between shell navigation pages", async () => {
    const api = makeApi();
    await createApp(document, api).init();

    document.querySelector<HTMLButtonElement>("[data-route='profiles']")?.click();

    expect(document.querySelector("#page-title")?.textContent).toBe("Profiles");
    expect(document.querySelector<HTMLElement>("[data-view='profiles']")?.hidden).toBe(false);
    expect(document.querySelector<HTMLElement>("[data-view='wifi']")?.hidden).toBe(true);
  });

  it("renders scanned networks and copies SSID into the form", async () => {
    const api = makeApi();
    const app = createApp(document, api);
    await app.init();
    await app.loadScan();

    const firstNetwork = document.querySelector<HTMLButtonElement>(".network-select");
    expect(firstNetwork?.textContent).toContain("Mock_Home_2G");
    firstNetwork?.click();

    expect((document.querySelector("#ssid") as HTMLInputElement).value).toBe("Mock_Home_2G");
    expect(document.querySelector("#scan-content")?.textContent).toContain("Development mock data");
  });

  it("renders saved profiles and connects using the selected profile", async () => {
    const api = makeApi();
    await createApp(document, api).init();

    expect(document.querySelector("#profiles-content")?.textContent).toContain("Mock_Home_2G");
    const fillButton = document.querySelector<HTMLButtonElement>("button[data-profile-action='fill']");
    fillButton?.click();

    expect((document.querySelector("#ssid") as HTMLInputElement).value).toBe("Mock_Home_2G");
    expect((document.querySelector("#password") as HTMLInputElement).value).toBe("mock-home-pass");

    const connectButton = document.querySelector<HTMLButtonElement>("button[data-profile-action='connect']");
    connectButton?.click();
    await vi.waitFor(() => expect(api.connectProfile).toHaveBeenCalledWith(0));
    await vi.waitFor(() =>
      expect(document.querySelector("#wifi-connection-content")?.textContent).toContain("192.168.0.25"),
    );
  });

  it("updates LED mode through the mocked backend", async () => {
    const api = makeApi();
    await createApp(document, api).init();

    const ledMode = document.querySelector("#led-mode") as HTMLSelectElement;
    ledMode.value = "identify";
    document.querySelector<HTMLFormElement>("#led-form")?.requestSubmit();
    await vi.waitFor(() => expect(api.setLedMode).toHaveBeenCalledWith("identify"));

    expect(document.querySelector("#led-message")?.textContent).toContain("LED mode updated");
  });
});
