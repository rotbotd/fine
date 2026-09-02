import { spawn } from "node:child_process";
import { readFile } from "node:fs/promises";
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

function request(headers = {}) {
  return new Promise((resolve, reject) => {
    const outgoing = http.get({ host: "127.0.0.1", port, path: "/fine.wasm", headers }, (response) => {
      const chunks = [];
      response.on("data", (chunk) => chunks.push(chunk));
      response.on("end", () => resolve({ headers: response.headers, body: Buffer.concat(chunks) }));
    });
    outgoing.on("error", reject);
  });
}

async function waitForServer() {
  for (let attempt = 0; attempt < 100; ++attempt) {
    try {
      return await request();
    } catch {
      await new Promise((resolve) => setTimeout(resolve, 50));
    }
  }
  throw new Error(`Vite preview did not start:\n${diagnostics}`);
}

try {
  const plain = await waitForServer();
  const compressed = await request({ "Accept-Encoding": "zstd" });
  const expectedPlain = await readFile("dist/fine.wasm");
  const expectedCompressed = await readFile("dist/fine.wasm.zst");

  if (!plain.body.equals(expectedPlain))
    throw new Error("plain Wasm response differs from the built module");
  if (compressed.headers["content-encoding"] !== "zstd")
    throw new Error(`expected zstd content encoding, got ${compressed.headers["content-encoding"]}`);
  if (!compressed.body.equals(expectedCompressed))
    throw new Error("zstd response differs from the precompressed module");
  console.log(`serve smoke passed: ${plain.body.length} -> ${compressed.body.length} bytes`);
} finally {
  server.kill("SIGTERM");
  await new Promise((resolve) => server.once("exit", resolve));
}
