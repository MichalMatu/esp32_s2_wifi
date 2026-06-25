import { mkdir, readFile, writeFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const repoRoot = path.resolve(root, "..");

const distDir = path.join(root, "dist");
const distHtmlPath = path.join(distDir, "index.html");
const headerPath = path.join(repoRoot, "src", "web_assets.h");

const distHtml = await readFile(distHtmlPath, "utf8");

const assetPath = (href) => path.join(distDir, href.replace(/^\//, ""));

const cssHref = distHtml.match(/<link rel="stylesheet" crossorigin href="([^"]+)">/)?.[1];
const jsSrc = distHtml.match(/<script type="module" crossorigin src="([^"]+)"><\/script>/)?.[1];

if (!cssHref || !jsSrc) {
  throw new Error("Cannot find Vite JS/CSS assets in dist/index.html");
}

const [css, js] = await Promise.all([readFile(assetPath(cssHref), "utf8"), readFile(assetPath(jsSrc), "utf8")]);

const page = distHtml
  .replace(/<link rel="stylesheet" crossorigin href="[^"]+">/, `<style>\n${css}\n</style>`)
  .replace(/<script type="module" crossorigin src="[^"]+"><\/script>/, "")
  .replace("</body>", `    <script type="module">\n${js}\n</script>\n  </body>`);

const toCString = (input) =>
  input
    .split("\n")
    .map((line) => JSON.stringify(`${line}\n`))
    .join("\n");

const header = `#pragma once

#include <stddef.h>

static const char WEB_INDEX_HTML[] =
${toCString(page)};

static const size_t WEB_INDEX_HTML_LEN = sizeof(WEB_INDEX_HTML) - 1;
`;

await mkdir(distDir, { recursive: true });
await Promise.all([writeFile(distHtmlPath, page), writeFile(headerPath, header)]);
