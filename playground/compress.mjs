import { readFile, writeFile } from "node:fs/promises";
import { promisify } from "node:util";
import { constants, zstdCompress } from "node:zlib";

const source = new URL("./dist/fine.wasm", import.meta.url);
const target = new URL("./dist/fine.wasm.zst", import.meta.url);
const input = await readFile(source);
const output = await promisify(zstdCompress)(input, {
  params: {
    [constants.ZSTD_c_compressionLevel]: 19,
  },
});

await writeFile(target, output);
console.log(`zstd: ${input.length} -> ${output.length} bytes`);
