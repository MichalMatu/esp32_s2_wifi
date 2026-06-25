import { readFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const repoRoot = path.resolve(root, "..");
const header = await readFile(path.join(repoRoot, "src", "web_assets.h"), "utf8");

if (!header.includes("<script type=\\\"module\\\">")) {
  throw new Error("Generated firmware HTML must keep inline JavaScript as type=module");
}

if (!header.includes("DOMContentLoaded")) {
  throw new Error("Generated firmware HTML must defer UI boot until DOMContentLoaded");
}

const ssidIndex = header.indexOf("id=\\\"ssid\\\"");
const scriptIndex = header.indexOf("<script type=\\\"module\\\">");
const bodyCloseIndex = header.indexOf("</body>");

if (ssidIndex === -1 || scriptIndex === -1 || bodyCloseIndex === -1) {
  throw new Error("Generated firmware HTML is missing expected form/script structure");
}

if (scriptIndex < ssidIndex || bodyCloseIndex < scriptIndex) {
  throw new Error("Generated firmware JavaScript must be emitted after the page markup");
}
