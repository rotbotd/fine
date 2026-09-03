import { createHash } from "node:crypto";
import { readFile, rename, writeFile } from "node:fs/promises";
import { promisify } from "node:util";
import { constants, zstdCompress } from "node:zlib";

async function prepare(prefix) {
  const source = new URL(`./public/${prefix}.wasm`, import.meta.url);
  const input = await readFile(source);
  const version = createHash("sha256").update(input).digest("hex").slice(0, 12);
  const moduleName = `${prefix}-${version}.mjs`;
  const wasmName = `${prefix}-${version}.wasm`;
  const output = await promisify(zstdCompress)(input, {
    params: {
      [constants.ZSTD_c_compressionLevel]: 19,
    },
  });
  await writeFile(new URL(`./public/${wasmName}.zst`, import.meta.url), output);
  await rename(new URL(`./public/${prefix}.mjs`, import.meta.url), new URL(`./public/${moduleName}`, import.meta.url));
  await rename(source, new URL(`./public/${wasmName}`, import.meta.url));
  console.log(`zstd ${prefix} ${version}: ${input.length} -> ${output.length} bytes`);
  return { moduleName, wasmName };
}

const ordinary = await prepare("fine");
const pthreads = await prepare("fine-pthreads");
await writeFile(
  new URL("./public/zstd-check.txt.zst", import.meta.url),
  await promisify(zstdCompress)(Buffer.from("fine-zstd-ok")),
);
await writeFile(
  new URL("./generated-assets.js", import.meta.url),
  `import createFineOrdinary from "/${ordinary.moduleName}";\n`
    + `import createFinePthreads from "/${pthreads.moduleName}";\n`
    + "export const pthreadCapable = globalThis.crossOriginIsolated === true "
    + "&& typeof globalThis.SharedArrayBuffer === \"function\";\n"
    + "export const createFine = pthreadCapable ? createFinePthreads : createFineOrdinary;\n"
    + `export const ordinaryWasm = pthreadCapable ? "/${pthreads.wasmName}" : "/${ordinary.wasmName}";\n`
    + `export const compressedWasm = pthreadCapable ? "/${pthreads.wasmName}.zst" : "/${ordinary.wasmName}.zst";\n`,
);
