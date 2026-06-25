import { httpApi } from "./api";
import { createApp } from "./app";

const showInitError = (error: unknown): void => {
  const message = error instanceof Error ? error.message : "unknown error";
  const target = document.querySelector<HTMLElement>("#wifi-message");
  if (target) {
    target.hidden = false;
    target.className = "notice error";
    target.textContent = `UI initialization failed: ${message}`;
  }
};

const boot = (): void => {
  void createApp(document, httpApi).init().catch(showInitError);
};

if (document.readyState === "loading") {
  document.addEventListener("DOMContentLoaded", boot, { once: true });
} else {
  boot();
}
