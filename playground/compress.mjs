import { createHash } from "node:crypto";
import { readFile, rename, writeFile } from "node:fs/promises";
import { promisify } from "node:util";
import { constants, zstdCompress } from "node:zlib";

const source = new URL("./public/fine.wasm", import.meta.url);
const input = await readFile(source);
const version = createHash("sha256").update(input).digest("hex").slice(0, 12);
const moduleName = `fine-${version}.mjs`;
const wasmName = `fine-${version}.wasm`;
const output = await promisify(zstdCompress)(input, {
  params: {
    [constants.ZSTD_c_compressionLevel]: 19,
  },
});

await writeFile(new URL(`./public/${wasmName}.zst`, import.meta.url), output);
await writeFile(
  new URL("./public/zstd-check.txt.zst", import.meta.url),
  await promisify(zstdCompress)(Buffer.from("fine-zstd-ok")),
);
await rename(new URL("./public/fine.mjs", import.meta.url), new URL(`./public/${moduleName}`, import.meta.url));
await rename(source, new URL(`./public/${wasmName}`, import.meta.url));
await writeFile(
  new URL("./generated-assets.js", import.meta.url),
  `import createFine from "/${moduleName}";\n`
    + `export { createFine };\n`
    + `export const ordinaryWasm = "/${wasmName}";\n`
    + `export const compressedWasm = "/${wasmName}.zst";\n`,
);
console.log(`zstd ${version}: ${input.length} -> ${output.length} bytes`);
