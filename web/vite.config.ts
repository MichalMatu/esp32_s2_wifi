import { defineConfig, type Plugin } from "vite";
import type { IncomingMessage } from "node:http";

const json = (value: unknown): string => JSON.stringify(value);

type MockProfile = {
  id: number;
  ssid: string;
  password: string;
};

const mockApi = (): Plugin => ({
  name: "esp32-mock-api",
  configureServer(server) {
    let configMode = "captive";
    let ledMode = "status";
    let ledState = "configuration";
    let scanState: "idle" | "running" | "done" = "idle";
    let scanStartedAt = 0;
    let profiles: MockProfile[] = [
      { id: 0, ssid: "Mock_Home_2G", password: "mock-home-pass" },
      { id: 1, ssid: "Mock_Workshop", password: "mock-workshop-pass" },
    ];

    const reindexProfiles = (): void => {
      profiles = profiles.map((profile, id) => ({ ...profile, id }));
    };

    const saveProfile = (ssid: string, password: string): void => {
      profiles = profiles.filter((profile) => profile.ssid !== ssid);
      profiles.unshift({ id: 0, ssid, password });
      profiles = profiles.slice(0, 8);
      reindexProfiles();
    };

    const readForm = async (req: IncomingMessage): Promise<URLSearchParams> => {
      let body = "";
      for await (const chunk of req) {
        body += chunk.toString();
      }
      return new URLSearchParams(body);
    };

    server.middlewares.use(async (req, res, next) => {
      if (!req.url?.startsWith("/api/")) {
        next();
        return;
      }

      const url = new URL(req.url, "http://localhost");
      res.setHeader("Content-Type", "application/json; charset=utf-8");
      res.setHeader("Cache-Control", "no-store");

      if (url.pathname === "/api/status") {
        res.end(
          json({
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
              configAccess: configMode === "local" ? "Local only" : "Captive portal",
              host:
                configMode === "local"
                  ? "wifi.local / 192.168.4.1"
                  : "wifi.local / wifi.settings / 192.168.4.1",
            },
            config: {
              activeMode: configMode,
              savedMode: configMode,
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
              mode: ledMode,
              state: ledState,
            },
          }),
        );
        return;
      }

      if (url.pathname === "/api/coredump") {
        res.setHeader("Content-Type", "application/octet-stream");
        res.setHeader("Content-Disposition", "attachment; filename=\"esp32-s2-coredump.elf\"");
        res.end("mock core dump");
        return;
      }

      if (url.pathname === "/api/coredump/erase") {
        if (req.method !== "POST") {
          res.statusCode = 405;
          res.end(json({ ok: false, message: "POST required." }));
          return;
        }
        res.end(json({ ok: true, message: "Core dump erased." }));
        return;
      }

      if (url.pathname === "/api/wifi/scan") {
        const scanDone = scanState === "running" && Date.now() - scanStartedAt > 900;
        if (scanDone) {
          scanState = "done";
        }
        if (req.method === "POST") {
          scanState = "running";
          scanStartedAt = Date.now();
        }

        const state = req.method === "POST" ? "running" : scanState;
        res.end(
          json({
            source: "mock",
            ok: true,
            state,
            total: 3,
            durationMs: state === "done" ? Date.now() - scanStartedAt : 0,
            networks:
              state === "done"
                ? [
                    { ssid: "Mock_Home_2G", rssi: -42, security: "WPA2", channel: 6 },
                    { ssid: "Mock_Workshop", rssi: -67, security: "WPA2/WPA3", channel: 11 },
                    { ssid: "Mock_OpenLab", rssi: -78, security: "open", channel: 1 },
                  ]
                : [],
          }),
        );
        return;
      }

      if (url.pathname === "/api/wifi/profiles") {
        res.end(json({ source: "mock", ok: true, profiles }));
        return;
      }

      if (url.pathname === "/api/wifi/connection") {
        res.end(
          json({
            source: "mock",
            connection: {
              ok: true,
              state: "succeeded",
              ssid: "Mock_Home_2G",
              message: "Wi-Fi test passed.",
              reason: "",
              ip: "192.168.0.25",
              rssi: -48,
              channel: 6,
              elapsedMs: 1200,
              bridgePending: false,
            },
          }),
        );
        return;
      }

      if (url.pathname === "/api/wifi/profile/connect") {
        if (req.method !== "POST") {
          res.statusCode = 405;
          res.end(json({ ok: false, message: "POST required." }));
          return;
        }
        const form = await readForm(req);
        const idParam = form.get("id");
        const id = idParam === null ? Number.NaN : Number(idParam);
        const profile = profiles.find((item) => item.id === id);
        if (!profile) {
          res.statusCode = 404;
          res.end(json({ ok: false, message: "Saved Wi-Fi profile was not found." }));
          return;
        }
        ledState = "saving";
        saveProfile(profile.ssid, profile.password);
        res.end(json({ ok: true, message: `Mock profile selected for ${profile.ssid}.` }));
        return;
      }

      if (url.pathname === "/api/wifi/profile/delete") {
        if (req.method !== "POST") {
          res.statusCode = 405;
          res.end(json({ ok: false, message: "POST required." }));
          return;
        }
        const form = await readForm(req);
        const idParam = form.get("id");
        const id = idParam === null ? Number.NaN : Number(idParam);
        const originalLength = profiles.length;
        profiles = profiles.filter((profile) => profile.id !== id);
        reindexProfiles();
        if (profiles.length === originalLength) {
          res.statusCode = 404;
          res.end(json({ ok: false, message: "Saved Wi-Fi profile was not found." }));
          return;
        }
        res.end(json({ ok: true, message: "Saved profile removed." }));
        return;
      }

      if (url.pathname === "/api/config") {
        if (req.method !== "POST") {
          res.statusCode = 405;
          res.end(json({ ok: false, message: "POST required." }));
          return;
        }
        const form = await readForm(req);
        const mode = form.get("mode");
        if (mode !== "local" && mode !== "captive") {
          res.statusCode = 400;
          res.end(json({ ok: false, message: "Invalid config access mode." }));
          return;
        }
        configMode = mode;
        const message =
          mode === "captive"
            ? "Captive portal saved. It can route Mac traffic to ESP next time config mode starts."
            : "Local-only mode saved. Mac Wi-Fi stays online next time config mode starts.";
        res.end(json({ ok: true, message }));
        return;
      }

      if (url.pathname === "/api/led") {
        if (req.method !== "POST") {
          res.statusCode = 405;
          res.end(json({ ok: false, message: "POST required." }));
          return;
        }
        const form = await readForm(req);
        const mode = form.get("mode");
        if (!mode || !["status", "on", "off", "identify"].includes(mode)) {
          res.statusCode = 400;
          res.end(json({ ok: false, message: "Invalid LED mode." }));
          return;
        }
        ledMode = mode;
        ledState = mode === "identify" ? "identify" : "configuration";
        res.end(json({ ok: true, message: "LED mode updated." }));
        return;
      }

      if (url.pathname === "/api/wifi/save") {
        if (req.method !== "POST") {
          res.statusCode = 405;
          res.end(json({ ok: false, message: "POST required." }));
          return;
        }
        const form = await readForm(req);
        const ssid = form.get("ssid");
        if (!ssid) {
          res.statusCode = 400;
          res.end(json({ ok: false, message: "SSID is required." }));
          return;
        }
        saveProfile(ssid, form.get("password") ?? "");
        ledState = "saving";
        res.end(json({ ok: true, message: `Wi-Fi credentials saved for ${ssid}.` }));
        return;
      }

      if (url.pathname === "/api/wifi/test" || url.pathname === "/api/wifi/reconnect") {
        if (req.method !== "POST") {
          res.statusCode = 405;
          res.end(json({ ok: false, message: "POST required." }));
          return;
        }
        const form = await readForm(req);
        const ssid = form.get("ssid");
        if (!ssid) {
          res.statusCode = 400;
          res.end(json({ ok: false, message: "SSID is required." }));
          return;
        }
        const reconnect = url.pathname === "/api/wifi/reconnect";
        if (reconnect) {
          saveProfile(ssid, form.get("password") ?? "");
        }
        ledState = reconnect ? "saving" : "configuration";
        res.end(
          json({
            source: "mock",
            connection: {
              ok: true,
              state: "succeeded",
              ssid,
              message: reconnect
                ? "Wi-Fi validated. Credentials saved. Restarting into bridge mode."
                : "Wi-Fi test passed. Credentials were not saved.",
              reason: "",
              ip: "192.168.0.25",
              rssi: -48,
              channel: 6,
              elapsedMs: 1200,
              bridgePending: reconnect,
            },
          }),
        );
        return;
      }

      res.statusCode = 404;
      res.end(json({ ok: false, message: "Unknown mock endpoint." }));
    });
  },
});

export default defineConfig({
  plugins: [mockApi()],
  server: {
    host: "127.0.0.1",
    port: 5173,
  },
  build: {
    outDir: "dist",
    emptyOutDir: true,
  },
  test: {
    environment: "jsdom",
    globals: true,
  },
});
