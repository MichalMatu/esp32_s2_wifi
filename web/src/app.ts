import type {
  ActionResponse,
  ApiClient,
  ConnectResponse,
  ProfilesResponse,
  ScanResponse,
  StatusResponse,
  WifiConnectionStatus,
  WifiProfile,
} from "./api";

type App = {
  init(): Promise<void>;
  loadStatus(): Promise<void>;
  loadScan(): Promise<void>;
  loadProfiles(): Promise<void>;
};

type Route = "wifi" | "profiles" | "diagnostics" | "settings" | "help";
type RouteMeta = { title: string; description: string };

const routes: Record<Route, RouteMeta> = {
  wifi: {
    title: "Wi-Fi",
    description: "Scan nearby 2.4 GHz networks or enter credentials manually.",
  },
  profiles: {
    title: "Profiles",
    description: "Use saved credentials without retyping passwords.",
  },
  diagnostics: {
    title: "Diagnostics",
    description: "Inspect firmware runtime, memory, and USB network state.",
  },
  settings: {
    title: "Settings",
    description: "Control USB configuration behavior and the status LED.",
  },
  help: {
    title: "Help",
    description: "Operational notes for configuration and bridge mode.",
  },
};

const escapeHtml = (value: string): string =>
  value.replace(/[&<>"']/g, (char) => {
    const entities: Record<string, string> = {
      "&": "&amp;",
      "<": "&lt;",
      ">": "&gt;",
      "\"": "&quot;",
      "'": "&#39;",
    };
    return entities[char] ?? char;
  });

const actionMessage = (result: ActionResponse, fallback: string): string =>
  result.message || fallback;

const sleep = (ms: number): Promise<void> =>
  new Promise((resolve) => window.setTimeout(resolve, ms));

export const createApp = (root: ParentNode, api: ApiClient): App => {
  const $ = <T extends HTMLElement>(selector: string): T => {
    const element = root.querySelector<T>(selector);
    if (!element) {
      throw new Error(`Missing element: ${selector}`);
    }
    return element;
  };

  const ssidInput = $("#ssid") as HTMLInputElement;
  const passwordInput = $("#password") as HTMLInputElement;
  const wifiForm = $("#wifi-form") as HTMLFormElement;
  const scanButton = $("#scan-button") as HTMLButtonElement;
  const saveButton = $("#save-button") as HTMLButtonElement;
  const testButton = $("#test-button") as HTMLButtonElement;
  const reconnectButton = $("#reconnect-button") as HTMLButtonElement;
  const scanContent = $("#scan-content");
  const configForm = $("#config-form") as HTMLFormElement;
  const configAccess = $("#config-access") as HTMLSelectElement;
  const ledForm = $("#led-form") as HTMLFormElement;
  const ledMode = $("#led-mode") as HTMLSelectElement;
  const profilesContent = $("#profiles-content");
  const showPasswords = $("#show-passwords") as HTMLInputElement;
  const refreshButton = $("#refresh-button") as HTMLButtonElement;
  const pageTitle = $("#page-title");
  const pageDescription = $("#page-description");
  const coreDumpDownload = $("#coredump-download") as HTMLAnchorElement;
  const coreDumpErase = $("#coredump-erase") as HTMLButtonElement;
  const connectionContent = $("#wifi-connection-content");
  let savedProfiles: WifiProfile[] = [];
  const wifiActionButtons = [saveButton, testButton, reconnectButton];

  const setRoute = (route: Route): void => {
    const meta = routes[route];
    pageTitle.textContent = meta.title;
    pageDescription.textContent = meta.description;

    root.querySelectorAll<HTMLElement>("[data-view]").forEach((view) => {
      const active = view.dataset.view === route;
      view.classList.toggle("active", active);
      view.hidden = !active;
    });

    root.querySelectorAll<HTMLButtonElement>("[data-route]").forEach((button) => {
      const active = button.dataset.route === route;
      button.classList.toggle("active", active);
      if (active) {
        button.setAttribute("aria-current", "page");
      } else {
        button.removeAttribute("aria-current");
      }
    });
  };

  const setNotice = (selector: string, message: string, kind: "ok" | "error" = "ok"): void => {
    const notice = $(selector);
    notice.hidden = false;
    notice.className = `notice ${kind}`;
    notice.textContent = message;
  };

  const readWifiCredentials = (): { ssid: string; password: string } | null => {
    const ssid = ssidInput.value.trim();
    if (!ssid) {
      setNotice("#wifi-message", "SSID is required.", "error");
      return null;
    }
    return { ssid, password: passwordInput.value };
  };

  const setWifiActionsDisabled = (disabled: boolean): void => {
    wifiActionButtons.forEach((button) => {
      button.disabled = disabled;
    });
  };

  const renderRows = (targetSelector: string, rows: Array<[string, string]>): void => {
    const target = $(targetSelector);
    target.innerHTML = rows
      .map(([label, value]) => `<tr><th>${escapeHtml(label)}</th><td>${escapeHtml(value)}</td></tr>`)
      .join("");
  };

  const connectionKind = (connection: WifiConnectionStatus): "ok" | "error" => {
    if (connection.state === "failed") {
      return "error";
    }
    return "ok";
  };

  const renderConnection = (connection: WifiConnectionStatus): void => {
    if (connection.state === "idle") {
      connectionContent.hidden = true;
      connectionContent.innerHTML = "";
      return;
    }

    connectionContent.hidden = false;
    const rows: Array<[string, string]> = [
      ["State", connection.state],
      ["SSID", connection.ssid || "-"],
      ["Message", connection.message || "-"],
    ];
    if (connection.reason) {
      rows.push(["Reason", connection.reason]);
    }
    if (connection.ip) {
      rows.push(["IP", connection.ip]);
    }
    if (connection.rssi !== null) {
      rows.push(["RSSI", `${connection.rssi} dBm`]);
    }
    if (connection.channel !== null) {
      rows.push(["Channel", String(connection.channel)]);
    }
    if (connection.bridgePending) {
      rows.push(["Next", "Restarting into bridge mode"]);
    }

    connectionContent.innerHTML =
      "<table><tbody>" +
      rows
        .map(([label, value]) => `<tr><th>${escapeHtml(label)}</th><td>${escapeHtml(value)}</td></tr>`)
        .join("") +
      "</tbody></table>";
  };

  const pollConnection = async (
    initial: ConnectResponse,
    noticeSelector: string,
  ): Promise<WifiConnectionStatus> => {
    let connection = initial.connection;
    renderConnection(connection);
    setNotice(noticeSelector, connection.message, connectionKind(connection));

    for (let attempt = 0; connection.state === "running" && attempt < 70; attempt++) {
      await sleep(500);
      connection = (await api.getConnection()).connection;
      renderConnection(connection);
      setNotice(noticeSelector, connection.message, connectionKind(connection));
    }

    return connection;
  };

  const setCoreDumpDownloadEnabled = (enabled: boolean): void => {
    coreDumpDownload.classList.toggle("disabled", !enabled);
    coreDumpDownload.setAttribute("aria-disabled", enabled ? "false" : "true");
    coreDumpDownload.tabIndex = enabled ? 0 : -1;
  };

  const renderProfiles = (response: ProfilesResponse): void => {
    if (!response.ok) {
      profilesContent.innerHTML = `<p class="muted">Saved profiles failed: ${escapeHtml(
        response.error ?? "unknown error",
      )}</p>`;
      savedProfiles = [];
      return;
    }

    savedProfiles = response.profiles;
    if (savedProfiles.length === 0) {
      profilesContent.innerHTML =
        "<p class=\"muted\">No saved networks yet. Save credentials once to add the first profile.</p>";
      return;
    }

    const rows = savedProfiles
      .map((profile) => {
        const password = profile.password || "(open network)";
        const passwordText = showPasswords.checked
          ? password
          : profile.password
            ? "********"
            : "(open network)";
        return (
          `<tr data-profile-id="${profile.id}">` +
          `<td class="ssid-cell" title="${escapeHtml(profile.ssid)}">` +
          `<strong>${escapeHtml(profile.ssid)}</strong></td>` +
          `<td class="password-cell">${escapeHtml(passwordText)}</td>` +
          "<td class=\"actions-cell\"><div class=\"profile-actions\">" +
          `<button class="secondary small" type="button" data-profile-action="fill" data-profile-id="${profile.id}">Fill</button>` +
          `<button class="secondary small" type="button" data-profile-action="connect" data-profile-id="${profile.id}">Connect</button>` +
          `<button class="secondary small" type="button" data-profile-action="delete" data-profile-id="${profile.id}">Forget</button>` +
          "</div></td></tr>"
        );
      })
      .join("");

    profilesContent.innerHTML =
      "<table><colgroup><col class=\"profile-ssid-col\"><col class=\"profile-password-col\">" +
      "<col class=\"profile-actions-col\"></colgroup>" +
      "<thead><tr><th>SSID</th><th>Password</th><th>Actions</th></tr></thead>" +
      `<tbody>${rows}</tbody></table>`;
  };

  const loadStatus = async (): Promise<void> => {
    const data: StatusResponse = await api.getStatus();

    configAccess.value = data.config.savedMode;
    ledMode.value = data.led.mode;
    $("#led-state").textContent = data.led.state;
    renderConnection(data.connection);

    renderRows("#system-table", [
      ["Runtime", data.system.runtime],
      ["CPU", data.system.cpu],
      ["Reset", data.system.reset],
      ["ESP-IDF", data.system.idf],
      ["Build", data.system.build],
    ]);

    renderRows("#memory-table", [
      ["Free heap", data.memory.freeHeap],
      ["Min free heap", data.memory.minFreeHeap],
      ["Flash chip", data.memory.flashChip],
      ["App partition", data.memory.appPartition],
    ]);

    const coreDumpState = data.coredump.present
      ? `available${data.coredump.error ? ` (${data.coredump.error})` : ""}`
      : "empty";
    renderRows("#coredump-table", [
      ["Enabled", data.coredump.enabled ? "yes" : "no"],
      ["Stored dump", coreDumpState],
      ["Size", data.coredump.size],
    ]);
    setCoreDumpDownloadEnabled(data.coredump.present);
    coreDumpErase.disabled = !data.coredump.present;

    renderRows("#network-table", [
      ["Mode", data.network.mode],
      ["USB", data.network.usb],
      ["Config access", data.network.configAccess],
      ["Host", data.network.host],
      ["Wi-Fi test", data.connection.state],
      ["Wi-Fi message", data.connection.message],
      ["LED mode", data.led.mode],
      ["LED state", data.led.state],
    ]);
  };

  const renderScan = (scan: ScanResponse): void => {
    if (scan.state === "idle") {
      scanContent.innerHTML = "<p class=\"muted\">Click Scan to search nearby 2.4 GHz networks.</p>";
      return;
    }

    if (scan.state === "running") {
      scanContent.innerHTML = "<p class=\"muted\">Scanning nearby 2.4 GHz networks.</p>";
      return;
    }

    if (!scan.ok) {
      scanContent.innerHTML = `<p class="muted">Wi-Fi scan failed: ${escapeHtml(scan.error ?? "unknown error")}</p>`;
      return;
    }

    if (scan.networks.length === 0) {
      scanContent.innerHTML = "<p class=\"muted\">No networks found.</p>";
      return;
    }

    const rows = scan.networks
      .map(
        (network) =>
          "<tr class=\"network-row\">" +
          `<td class="ssid-cell">` +
          `<button class="network-select" type="button" data-ssid="${escapeHtml(network.ssid)}" title="${escapeHtml(network.ssid)}">` +
          `<strong>${escapeHtml(network.ssid)}</strong>` +
          "</button></td>" +
          `<td>${network.rssi} dBm</td>` +
          `<td title="${escapeHtml(network.security)}">${escapeHtml(network.security)}</td>` +
          `<td>${network.channel}</td>` +
          "</tr>",
      )
      .join("");

    const sourceNote =
      scan.source === "mock"
        ? " Development mock data from Vite. Flash the firmware and open 192.168.4.1 for a real scan."
        : "";
    const durationNote = scan.durationMs > 0 ? ` Scan took ${scan.durationMs} ms.` : "";

    scanContent.innerHTML =
      "<table><colgroup><col class=\"ssid-col\"><col class=\"rssi-col\">" +
      "<col class=\"security-col\"><col class=\"channel-col\"></colgroup>" +
      "<thead><tr><th>SSID</th><th>RSSI</th><th>Security</th><th>CH</th></tr></thead>" +
      `<tbody>${rows}</tbody></table>` +
      `<p class="muted">Showing ${scan.networks.length} of ${scan.total} detected networks.${durationNote}${sourceNote}</p>`;

    root.querySelectorAll<HTMLButtonElement>(".network-select").forEach((button) => {
      button.addEventListener("click", () => {
        ssidInput.value = button.dataset.ssid ?? "";
        passwordInput.focus();
      });
    });
  };

  const loadScan = async (): Promise<void> => {
    scanButton.disabled = true;
    scanButton.textContent = "Scanning...";
    const restoreRefresh = !refreshButton.disabled;
    if (restoreRefresh) {
      refreshButton.disabled = true;
      refreshButton.textContent = "Scan running";
    }
    scanContent.innerHTML = "<p class=\"muted\">Scanning nearby 2.4 GHz networks.</p>";
    try {
      let scan = await api.startScan();
      renderScan(scan);
      for (let attempt = 0; scan.state === "running" && attempt < 90; attempt++) {
        await sleep(500);
        scan = await api.getScan();
        renderScan(scan);
      }
      if (scan.state === "running") {
        scanContent.innerHTML =
          "<p class=\"muted\">Wi-Fi scan is still running. Keep the USB link connected and wait a moment.</p>";
      }
    } catch (error) {
      const message = error instanceof Error ? error.message : "unknown error";
      scanContent.innerHTML =
        `<p class="muted">Wi-Fi scan request failed: ${escapeHtml(message)}. ` +
        "Check the USB link and click Scan again.</p>";
    } finally {
      scanButton.disabled = false;
      scanButton.textContent = "Scan";
      if (restoreRefresh) {
        refreshButton.disabled = false;
        refreshButton.textContent = "Refresh data";
      }
    }
  };

  const loadProfiles = async (): Promise<void> => {
    try {
      renderProfiles(await api.getProfiles());
    } catch (error) {
      const message = error instanceof Error ? error.message : "unknown error";
      profilesContent.innerHTML = `<p class="muted">Saved profiles failed: ${escapeHtml(message)}</p>`;
      savedProfiles = [];
    }
  };

  const refreshData = async (): Promise<void> => {
    refreshButton.disabled = true;
    refreshButton.textContent = "Refreshing...";
    try {
      await Promise.allSettled([loadStatus(), loadProfiles()]);
    } finally {
      refreshButton.disabled = false;
      refreshButton.textContent = "Refresh data";
    }
  };

  const findProfile = (id: number): WifiProfile | undefined =>
    savedProfiles.find((profile) => profile.id === id);

  const init = async (): Promise<void> => {
    root.querySelectorAll<HTMLButtonElement>("[data-route]").forEach((button) => {
      button.addEventListener("click", () => {
        const route = button.dataset.route as Route | undefined;
        if (route && route in routes) {
          setRoute(route);
        }
      });
    });

    refreshButton.addEventListener("click", () => {
      void refreshData();
    });

    saveButton.addEventListener("click", async () => {
      const credentials = readWifiCredentials();
      if (!credentials) {
        return;
      }

      setWifiActionsDisabled(true);
      saveButton.textContent = "Saving...";
      try {
        const result = await api.saveWifi(credentials.ssid, credentials.password);
        setNotice("#wifi-message", actionMessage(result, "Wi-Fi credentials saved."), result.ok ? "ok" : "error");
        if (result.ok) {
          await loadProfiles();
        }
      } catch (error) {
        const message = error instanceof Error ? error.message : "unknown error";
        setNotice("#wifi-message", `Wi-Fi save failed: ${message}`, "error");
      } finally {
        saveButton.textContent = "Save";
        setWifiActionsDisabled(false);
      }
    });

    wifiForm.addEventListener("submit", async (event) => {
      event.preventDefault();
      const credentials = readWifiCredentials();
      if (!credentials) {
        return;
      }

      setWifiActionsDisabled(true);
      testButton.textContent = "Testing...";
      try {
        await pollConnection(await api.testWifi(credentials.ssid, credentials.password), "#wifi-message");
      } catch (error) {
        const message = error instanceof Error ? error.message : "unknown error";
        setNotice("#wifi-message", `Wi-Fi test failed: ${message}`, "error");
      } finally {
        testButton.textContent = "Test";
        setWifiActionsDisabled(false);
      }
    });

    reconnectButton.addEventListener("click", async () => {
      const credentials = readWifiCredentials();
      if (!credentials) {
        return;
      }

      setWifiActionsDisabled(true);
      reconnectButton.textContent = "Connecting...";
      try {
        const connection = await pollConnection(
          await api.connectWifi(credentials.ssid, credentials.password),
          "#wifi-message",
        );
        if (connection.state === "succeeded") {
          await loadProfiles();
        }
      } catch (error) {
        const message = error instanceof Error ? error.message : "unknown error";
        setNotice("#wifi-message", `Wi-Fi connect failed: ${message}`, "error");
      } finally {
        reconnectButton.textContent = "Connect and restart";
        setWifiActionsDisabled(false);
      }
    });

    configForm.addEventListener("submit", async (event) => {
      event.preventDefault();
      try {
        const result = await api.setConfigMode(configAccess.value);
        setNotice(
          "#config-message",
          actionMessage(result, "Config access mode saved."),
          result.ok ? "ok" : "error",
        );
        await loadStatus();
      } catch (error) {
        const message = error instanceof Error ? error.message : "unknown error";
        setNotice("#config-message", `Config access update failed: ${message}`, "error");
      }
    });

    ledForm.addEventListener("submit", async (event) => {
      event.preventDefault();
      try {
        const result = await api.setLedMode(ledMode.value);
        setNotice(
          "#led-message",
          actionMessage(result, "LED mode updated."),
          result.ok ? "ok" : "error",
        );
        await loadStatus();
      } catch (error) {
        const message = error instanceof Error ? error.message : "unknown error";
        setNotice("#led-message", `LED update failed: ${message}`, "error");
      }
    });

    coreDumpDownload.addEventListener("click", (event) => {
      if (coreDumpDownload.getAttribute("aria-disabled") === "true") {
        event.preventDefault();
      }
    });

    coreDumpErase.addEventListener("click", async () => {
      if (!window.confirm("Erase stored core dump from flash?")) {
        return;
      }
      coreDumpErase.disabled = true;
      let refreshed = false;
      try {
        const result = await api.eraseCoreDump();
        setNotice(
          "#coredump-message",
          actionMessage(result, "Core dump erased."),
          result.ok ? "ok" : "error",
        );
        await loadStatus();
        refreshed = true;
      } catch (error) {
        const message = error instanceof Error ? error.message : "unknown error";
        setNotice("#coredump-message", `Core dump erase failed: ${message}`, "error");
      } finally {
        if (!refreshed) {
          coreDumpErase.disabled = false;
        }
      }
    });

    scanButton.addEventListener("click", () => {
      void loadScan();
    });

    showPasswords.addEventListener("change", () => {
      renderProfiles({ source: "device", ok: true, profiles: savedProfiles });
    });

    profilesContent.addEventListener("click", async (event) => {
      const button = (event.target as HTMLElement).closest<HTMLButtonElement>(
        "button[data-profile-action]",
      );
      if (!button) {
        return;
      }

      const id = Number(button.dataset.profileId);
      const profile = findProfile(id);
      if (!profile) {
        setNotice("#profiles-message", "Saved profile was not found.", "error");
        await loadProfiles();
        return;
      }

      const action = button.dataset.profileAction;
      if (action === "fill") {
        ssidInput.value = profile.ssid;
        passwordInput.value = profile.password;
        setRoute("wifi");
        passwordInput.focus();
        setNotice("#wifi-message", `Loaded ${profile.ssid} from saved profiles.`);
        return;
      }

      if (action === "connect") {
        button.disabled = true;
        button.textContent = "Connecting...";
        try {
          const result = await api.connectProfile(id);
          setRoute("wifi");
          const connection = await pollConnection(result, "#wifi-message");
          setNotice("#profiles-message", connection.message, connectionKind(connection));
        } catch (error) {
          const message = error instanceof Error ? error.message : "unknown error";
          setNotice("#profiles-message", `Profile connect failed: ${message}`, "error");
        } finally {
          button.disabled = false;
          button.textContent = "Connect";
        }
        return;
      }

      if (action === "delete") {
        if (!window.confirm(`Forget saved network ${profile.ssid}?`)) {
          return;
        }
        button.disabled = true;
        try {
          const result = await api.deleteProfile(id);
          setNotice(
            "#profiles-message",
            actionMessage(result, "Saved profile removed."),
            result.ok ? "ok" : "error",
          );
          await loadProfiles();
        } catch (error) {
          const message = error instanceof Error ? error.message : "unknown error";
          setNotice("#profiles-message", `Profile delete failed: ${message}`, "error");
        } finally {
          button.disabled = false;
        }
      }
    });

    setRoute("wifi");
    await refreshData();
  };

  return { init, loadStatus, loadScan, loadProfiles };
};
