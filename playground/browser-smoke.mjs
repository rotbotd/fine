import { spawn } from "node:child_process";
import { mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import net from "node:net";
import os from "node:os";
import path from "node:path";

const [sourcePath, specializedPath, fontDirectory, runtime] = process.argv.slice(2);
if (!sourcePath || !specializedPath || !fontDirectory
    || !["ordinary", "pthreads"].includes(runtime)) {
  throw new Error("usage: node browser-smoke.mjs SOURCE SPECIALIZED_SOURCE FONT_DIRECTORY ordinary|pthreads");
}

const expectedSource = await readFile(sourcePath, "utf8");
const expectedSpecialized = await readFile(specializedPath, "utf8");
const profile = await mkdtemp(path.join(os.tmpdir(), "fine-browser-smoke-"));
const fontConfig = path.join(profile, "fonts.conf");
await writeFile(fontConfig, `<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "urn:fontconfig:fonts.dtd">
<fontconfig>
  <dir>${fontDirectory}</dir>
  <cachedir>${path.join(profile, "font-cache")}</cachedir>
</fontconfig>
`);

function reservePort() {
  return new Promise((resolve, reject) => {
    const socket = net.createServer();
    socket.once("error", reject);
    socket.listen(0, "127.0.0.1", () => {
      const port = socket.address().port;
      socket.close((error) => error ? reject(error) : resolve(port));
    });
  });
}

function delay(milliseconds) {
  return new Promise((resolve) => setTimeout(resolve, milliseconds));
}

const port = await reservePort();
const server = spawn("./node_modules/.bin/vite", [
  "preview",
  "--config", "vite.config.js",
  "--configLoader", "native",
  "--outDir", "dist",
  "--host", "127.0.0.1",
  "--port", String(port),
  "--strictPort",
], {
  stdio: ["ignore", "pipe", "pipe"],
});

const browser = spawn("chromium", [
  "--headless=new",
  "--no-sandbox",
  "--disable-gpu",
  "--disable-background-networking",
  "--remote-debugging-port=0",
  `--user-data-dir=${profile}`,
  "about:blank",
], {
  env: { ...process.env, FONTCONFIG_FILE: fontConfig },
  stdio: ["ignore", "pipe", "pipe"],
});

let diagnostics = "";
for (const child of [server, browser]) {
  child.stdout.on("data", (chunk) => { diagnostics += chunk; });
  child.stderr.on("data", (chunk) => { diagnostics += chunk; });
}

async function waitForDebugPort() {
  const activePort = path.join(profile, "DevToolsActivePort");
  for (let attempt = 0; attempt < 600; ++attempt) {
    try {
      const [line] = (await readFile(activePort, "utf8")).split("\n");
      if (line)
        return Number.parseInt(line, 10);
    } catch {
      // Chromium writes the file only after its DevTools listener is ready.
    }
    if (browser.exitCode !== null)
      throw new Error(`Chromium exited before DevTools was ready:\n${diagnostics}`);
    await delay(50);
  }
  throw new Error(`Chromium did not expose DevTools:\n${diagnostics}`);
}

async function waitForServer(url) {
  for (let attempt = 0; attempt < 600; ++attempt) {
    try {
      const response = await fetch(url);
      if (response.ok)
        return;
    } catch {
      // The Vite listener and Chromium start concurrently.
    }
    if (server.exitCode !== null)
      throw new Error(`Vite exited before serving the playground:\n${diagnostics}`);
    await delay(50);
  }
  throw new Error(`Vite did not serve the playground:\n${diagnostics}`);
}

async function stopChild(child) {
  if (child.exitCode !== null)
    return;
  function waitForExit() {
    return new Promise((resolve) => {
      child.once("exit", resolve);
      child.once("error", resolve);
      queueMicrotask(() => {
        if (child.exitCode !== null)
          resolve();
      });
    });
  }
  let exited = waitForExit();
  child.kill("SIGTERM");
  const graceful = await Promise.race([
    exited.then(() => true),
    delay(2000).then(() => false),
  ]);
  if (!graceful) {
    if (child.exitCode === null)
      child.kill("SIGKILL");
    exited = waitForExit();
    await Promise.race([exited, delay(2000)]);
  }
}

let socket;
let target;
try {
  const debugPort = await waitForDebugPort();
  console.log("browser smoke: DevTools ready");
  const debug = `http://127.0.0.1:${debugPort}`;
  const url = `http://127.0.0.1:${port}/`;
  await waitForServer(url);
  console.log("browser smoke: Vite ready");
  target = await fetch(`${debug}/json/new?${encodeURIComponent("about:blank")}`, {
    method: "PUT",
  }).then((response) => response.json());

  socket = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((resolve, reject) => {
    socket.addEventListener("open", resolve, { once: true });
    socket.addEventListener("error", reject, { once: true });
  });
  console.log("browser smoke: page target connected");

  let nextRequest = 1;
  const pending = new Map();
  const consoleErrors = [];
  socket.addEventListener("message", ({ data }) => {
    const message = JSON.parse(data);
    if (message.method === "Runtime.exceptionThrown")
      consoleErrors.push(message.params.exceptionDetails.text);
    if (message.method === "Runtime.consoleAPICalled"
        && ["error", "assert"].includes(message.params.type)) {
      consoleErrors.push(message.params.args.map((argument) => argument.value ?? argument.description).join(" "));
    }
    const request = pending.get(message.id);
    if (!request)
      return;
    pending.delete(message.id);
    if (message.error)
      request.reject(new Error(`${request.method}: ${JSON.stringify(message.error)}`));
    else
      request.resolve(message.result);
  });

  function send(method, params = {}) {
    const id = nextRequest++;
    socket.send(JSON.stringify({ id, method, params }));
    return new Promise((resolve, reject) => {
      const timeout = setTimeout(() => {
        pending.delete(id);
        reject(new Error(`${method} did not answer within thirty seconds`));
      }, 30000);
      pending.set(id, {
        method,
        resolve: (value) => { clearTimeout(timeout); resolve(value); },
        reject: (error) => { clearTimeout(timeout); reject(error); },
      });
    });
  }

  async function evaluate(expression) {
    const response = await send("Runtime.evaluate", {
      expression,
      awaitPromise: true,
      returnByValue: true,
    });
    if (response.exceptionDetails) {
      throw new Error(response.exceptionDetails.exception?.description
        ?? response.exceptionDetails.text
        ?? "browser evaluation failed");
    }
    return response.result.value;
  }

  async function waitFor(expression, description) {
    for (let attempt = 0; attempt < 1200; ++attempt) {
      const value = await evaluate(expression);
      if (value)
        return value;
      await delay(50);
    }
    throw new Error(`timed out waiting for ${description}`);
  }

  const editorSource = `document.querySelector(".cm-content")?.cmTile?.view?.state?.doc?.toString()`;
  await send("Runtime.enable");
  await send("Page.enable");
  if (runtime === "ordinary") {
    await send("Page.addScriptToEvaluateOnNewDocument", {
      source: `Object.defineProperty(globalThis, "SharedArrayBuffer", { value: undefined });`,
    });
  }
  await send("Page.navigate", { url });
  await waitFor(`document.querySelector("#status")?.textContent === "ready"`, "Fine to load");
  console.log("browser smoke: Fine ready");

  const expectedRuntime = runtime === "ordinary" ? "single-threaded" : "pthreads";
  if (!await evaluate(`crossOriginIsolated && document.documentElement.dataset.fineRuntime === ${JSON.stringify(expectedRuntime)}`))
    throw new Error(`browser smoke did not select the isolated ${runtime} runtime`);
  if (await evaluate(editorSource) !== expectedSource)
    throw new Error("CodeMirror did not receive the exact checked default source");

  await evaluate(`document.querySelector("#specialize").click()`);
  await waitFor(`document.querySelector("#status")?.textContent === "specialized"`, "specialization");
  if (await evaluate(editorSource) !== expectedSpecialized)
    throw new Error("the specialization button did not install the exact checked source");

  await evaluate(`document.querySelector(".cm-content").focus()`);
  await send("Input.dispatchKeyEvent", {
    type: "rawKeyDown", key: "z", code: "KeyZ",
    windowsVirtualKeyCode: 90, nativeVirtualKeyCode: 90, modifiers: 2,
  });
  await send("Input.dispatchKeyEvent", {
    type: "keyUp", key: "z", code: "KeyZ",
    windowsVirtualKeyCode: 90, nativeVirtualKeyCode: 90, modifiers: 2,
  });
  await waitFor(`${editorSource} === ${JSON.stringify(expectedSource)}`, "one-step undo");

  await evaluate(`document.querySelector("#specialize-function").value = "missing_wrapper"`);
  await evaluate(`document.querySelector("#specialize").click()`);
  await waitFor(`document.querySelector("#status")?.textContent === "specialization failed"`, "failed specialization");
  if (await evaluate(editorSource) !== expectedSource)
    throw new Error("failed specialization changed the editor source");
  if (!await evaluate(`!document.querySelector("#specialize").disabled`))
    throw new Error("failed specialization left the browser action disabled");
  if (consoleErrors.length > 0)
    throw new Error(`browser console errors:\n${consoleErrors.join("\n")}`);

  console.log(`browser smoke passed on ${runtime}: button installed exact source, one undo restored it, failure made no edit`);
  await fetch(`${debug}/json/close/${target.id}`);
} catch (error) {
  console.error(diagnostics);
  throw error;
} finally {
  socket?.close();
  await Promise.all([browser, server].map(stopChild));
  await rm(profile, { recursive: true, force: true, maxRetries: 20, retryDelay: 100 });
}
