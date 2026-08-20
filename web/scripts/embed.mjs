import { gzipSync } from "node:zlib";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import { resolve } from "node:path";

const root = resolve(import.meta.dirname, "..");
const output = resolve(root, "../components/airscope_api/web");
const assets = [
  ["dist/index.html", "index.html.gz"],
  ["dist/assets/app.js", "app.js.gz"],
  ["dist/assets/app.css", "app.css.gz"],
];

await mkdir(output, { recursive: true });
for (const [source, target] of assets) {
  const content = await readFile(resolve(root, source));
  await writeFile(resolve(output, target), gzipSync(content, { level: 9 }));
}
