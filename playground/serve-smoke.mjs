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
  const pthreadWasmName = files.find((file) => /^fine-pthreads-[0-9a-f]+\.wasm$/.test(file));
  const pthreadModuleName = files.find((file) => /^fine-pthreads-[0-9a-f]+\.mjs$/.test(file));
  if (!wasmName)
    throw new Error("versioned Wasm asset is missing");
  if (!pthreadWasmName)
    throw new Error("versioned pthread Wasm asset is missing");
  if (!pthreadModuleName)
    throw new Error("versioned pthread JavaScript module is missing");
  const pthreadGlue = await readFile(`dist/${pthreadModuleName}`, "utf8");
  if (!pthreadGlue.includes(`new URL("${pthreadModuleName}",import.meta.url)`))
    throw new Error("pthread worker does not reload the hashed pthread module");
  const wasmPath = `/${wasmName}`;
  const plain = await waitForServer(wasmPath);
  const compressed = await request(wasmPath, { "Accept-Encoding": "zstd" });
  const explicit = await request(`${wasmPath}.zst`);
  const page = await request("/");
  const pthreadWasm = await request(`/${pthreadWasmName}`);
  const expectedPlain = await readFile(`dist/${wasmName}`);
  const expectedCompressed = await readFile(`dist/${wasmName}.zst`);

  if (!plain.body.equals(expectedPlain))
    throw new Error("plain Wasm response differs from the built module");
  for (const [name, response] of [["page", page], ["wasm", plain], ["pthread wasm", pthreadWasm]]) {
    if (response.headers["cross-origin-opener-policy"] !== "same-origin"
        || response.headers["cross-origin-embedder-policy"] !== "require-corp")
      throw new Error(`${name} response is not cross-origin isolated`);
  }
  if (!String(plain.headers["cache-control"] ?? "").startsWith("public"))
    throw new Error(`plain Wasm response is not cacheable: ${plain.headers["cache-control"]}`);
  if (compressed.headers["content-encoding"] !== "zstd")
    throw new Error(`expected zstd content encoding, got ${compressed.headers["content-encoding"]}`);
  if (!compressed.body.equals(expectedCompressed))
    throw new Error("zstd response differs from the precompressed module");
  if (explicit.headers["content-encoding"] !== "zstd" || !explicit.body.equals(expectedCompressed))
    throw new Error("explicit zstd Wasm response is not the precompressed module");
  const reference = page.body.toString("utf8");
  if (!reference.includes('id="materialize"') || !reference.includes("materialize holes"))
    throw new Error("served playground is missing the materialize action");
  for (const control of ['id="checkpoint"', 'id="stop-checkpoint"', 'id="checkpoint-budget"'])
    if (!reference.includes(control))
      throw new Error(`served playground is missing checkpoint control: ${control}`);
  if (!reference.includes("identity holes in source order inside Wasm")
      || !reference.includes("every view retains earlier completed holes")
      || !reference.includes("shared ring") || !reference.includes("last validated source"))
    throw new Error("language reference does not state the checkpoint interruption boundary");
  for (const form of ["runtime enum", "runtime match", "indexed proof family", "indexed constructor evidence", "indexed proof match", "staged proof match", "structural proof induction", "indexed proof hole", "partial proof checkpoint", "empty proof match"])
    if (!reference.includes(form))
      throw new Error(`language reference is missing current form: ${form}`);
  if (!reference.includes("takes [") || reference.includes("<code>needs ["))
    throw new Error("language reference does not expose only the current static-input keyword");
  for (const currentExample of ["proof function even_pred", "proof copied: Even(zero) = ?;", "proof function plus_shift", "function recover(value: Flag)"])
    if (!reference.includes(currentExample))
      throw new Error(`language reference omits checked current example: ${currentExample}`);
  for (const staleExample of ["Rebuilt(value)", "shape_zero", "rebuilt_next"])
    if (reference.includes(staleExample))
      throw new Error(`language reference retains undeclared example vocabulary: ${staleExample}`);
  if (!reference.includes("accepted excerpts from checked fixtures"))
    throw new Error("language reference does not state the provenance of its examples");
  if (!reference.includes("Top-level declarations may be interleaved")
      || !reference.includes("definition-only document needs no <code>run</code>"))
    throw new Error("language reference still advertises phased declarations or a mandatory run");
  const bundleName = reference.match(/\/assets\/index-[^"']+\.js/)?.[0];
  if (!bundleName)
    throw new Error("served playground does not reference its application bundle");
  const bundle = await request(bundleName);
  const bundleText = bundle.body.toString("utf8");
  if (!bundleText.includes("crossOriginIsolated") || !bundleText.includes("SharedArrayBuffer")
      || !bundleText.includes("fineRuntime") || !bundleText.includes("pthreads"))
    throw new Error("application bundle omits pthread feature detection and runtime disclosure");
  if (!bundleText.includes("validate-checkpoint") || !bundleText.includes("fineLiveSequence")
      || !bundleText.includes("Atomics"))
    throw new Error("application bundle omits live mailbox source validation");
  if (!reference.includes("two distinct IH edges"))
    throw new Error("language reference omits branching structural induction");
  console.log(`serve smoke passed: ${plain.body.length} -> ${compressed.body.length} bytes`);
} finally {
  server.kill("SIGTERM");
  await new Promise((resolve) => server.once("exit", resolve));
}
