import { readdir, readFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const srcDir = path.join(root, "src");
const indexHtml = path.join(root, "index.html");

const failures = [];
const cssFiles = (await readdir(srcDir))
  .filter((file) => file.endsWith(".css"))
  .map((file) => path.join(srcDir, file));

const componentCss = cssFiles.filter((file) => path.basename(file) !== "tokens.css");

for (const file of componentCss) {
  const relative = path.relative(root, file);
  const css = await readFile(file, "utf8");

  if (relative === "src/styles.css" && !css.startsWith("@import \"./tokens.css\";")) {
    failures.push(`${relative}: styles.css must import tokens.css first`);
  }

  const rawColor = css.match(/#[0-9a-fA-F]{3,8}\b|rgba?\(|hsla?\(/);
  if (rawColor) {
    failures.push(`${relative}: raw color "${rawColor[0]}" found outside tokens.css`);
  }

  const radiusMatches = css.matchAll(/border-radius:\s*([^;]+)/g);
  for (const match of radiusMatches) {
    const value = match[1].trim();
    if (!value.startsWith("var(") && value !== "0") {
      failures.push(`${relative}: border radius must use a radius token (border-radius: ${value})`);
    }
  }

  const shadowMatches = css.matchAll(/box-shadow:\s*([^;]+)/g);
  for (const match of shadowMatches) {
    const value = match[1].trim();
    if (!value.startsWith("var(") && value !== "none") {
      failures.push(`${relative}: shadow must use a shadow token (box-shadow: ${value})`);
    }
  }

  if (css.includes("!important")) {
    failures.push(`${relative}: !important is not allowed`);
  }
}

const html = await readFile(indexHtml, "utf8");
if (/\sstyle=/.test(html)) {
  failures.push("index.html: inline style attributes are not allowed");
}

if (failures.length > 0) {
  console.error(failures.join("\n"));
  process.exit(1);
}
