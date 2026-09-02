import { spawn } from "node:child_process";
import { readFile, readdir } from "node:fs/promises";
import http from "node:http";

const port = 30000 + (process.pid % 20000);
const server = spawn("./node_modules/.bin/vite", [
  "preview",
  "--config", "vite.config.js",
  "--configLoader", "native",
  "--outDir", "dist",
  "--host", "127.0.0.1",
  "--port", String(port),
  "--strictPort",
], { stdio: ["ignore", "pipe", "pipe"] });

let diagnostics = "";
server.stdout.on("data", (chunk) => { diagnostics += chunk; });
server.stderr.on("data", (chunk) => { diagnostics += chunk; });

function request(path, headers = {}) {
  return new Promise((resolve, reject) => {
    const outgoing = http.get({ host: "127.0.0.1", port, path, headers }, (response) => {
      const chunks = [];
      response.on("data", (chunk) => chunks.push(chunk));
      response.on("end", () => resolve({ headers: response.headers, body: Buffer.concat(chunks) }));
    });
    outgoing.on("error", reject);
  });
}

async function waitForServer(wasmPath) {
  for (let attempt = 0; attempt < 100; ++attempt) {
    try {
      return await request(wasmPath);
    } catch {
      await new Promise((resolve) => setTimeout(resolve, 50));
    }
  }
  throw new Error(`Vite preview did not start:\n${diagnostics}`);
}

try {
  const files = await readdir("dist");
  const wasmName = files.find((file) => /^fine-[0-9a-f]+\.wasm$/.test(file));
  if (!wasmName)
    throw new Error("versioned Wasm asset is missing");
  const wasmPath = `/${wasmName}`;
  const plain = await waitForServer(wasmPath);
  const compressed = await request(wasmPath, { "Accept-Encoding": "zstd" });
  const explicit = await request(`${wasmPath}.zst`);
  const page = await request("/");
  const expectedPlain = await readFile(`dist/${wasmName}`);
  const expectedCompressed = await readFile(`dist/${wasmName}.zst`);

  if (!plain.body.equals(expectedPlain))
    throw new Error("plain Wasm response differs from the built module");
  if (!String(plain.headers["cache-control"] ?? "").startsWith("public"))
    throw new Error(`plain Wasm response is not cacheable: ${plain.headers["cache-control"]}`);
  if (compressed.headers["content-encoding"] !== "zstd")
    throw new Error(`expected zstd content encoding, got ${compressed.headers["content-encoding"]}`);
  if (!compressed.body.equals(expectedCompressed))
    throw new Error("zstd response differs from the precompressed module");
  if (explicit.headers["content-encoding"] !== "zstd" || !explicit.body.equals(expectedCompressed))
    throw new Error("explicit zstd Wasm response is not the precompressed module");
  const reference = page.body.toString("utf8");
  for (const form of ["runtime enum", "runtime match", "indexed proof family", "indexed constructor evidence", "indexed proof match", "empty proof match"])
    if (!reference.includes(form))
      throw new Error(`language reference is missing current form: ${form}`);
  console.log(`serve smoke passed: ${plain.body.length} -> ${compressed.body.length} bytes`);
} finally {
  server.kill("SIGTERM");
  await new Promise((resolve) => server.once("exit", resolve));
}
