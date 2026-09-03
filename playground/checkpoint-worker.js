import { compressedWasm, createFine, ordinaryWasm } from "./generated-assets.js";
import { runCheckpointEpoch } from "./checkpoint-epoch.js";

let capture = null;

let useCompressedWasm = false;
try {
  const probe = await fetch("/zstd-check.txt.zst", { cache: "no-store" });
  useCompressedWasm = probe.ok && await probe.text() === "fine-zstd-ok";
} catch {
  // Workers without HTTP Zstandard support use the ordinary Wasm response.
}

const fine = await createFine({
  locateFile(file) {
    if (file !== "fine.wasm")
      return file;
    return useCompressedWasm ? compressedWasm : ordinaryWasm;
  },
  print(line) {
    capture?.stdout.push(line);
  },
  printErr(line) {
    capture?.stderr.push(line);
  },
});

function invoke(args) {
  capture = { stdout: [], stderr: [] };
  let code = 0;
  try {
    code = fine.callMain(args) ?? 0;
  } catch (error) {
    if (typeof error?.status === "number")
      code = error.status;
    else
      throw error;
  }
  const completed = capture;
  capture = null;
  completed.code = code;
  return completed;
}

self.addEventListener("message", async ({ data }) => {
  if (data?.type !== "start")
    return;
  let source = data.source;
  try {
    for (let epoch = 1; epoch <= data.maxEpochs; ++epoch) {
      const checkpoint = runCheckpointEpoch(fine, invoke, source, data.budget, epoch);
      if (checkpoint === source) {
        self.postMessage({ type: "done", epoch: epoch - 1, source });
        return;
      }
      source = checkpoint;
      self.postMessage({ type: "epoch", epoch, source });
      await new Promise((resolve) => setTimeout(resolve, 0));
    }
    self.postMessage({ type: "limit", epoch: data.maxEpochs, source });
  } catch (error) {
    self.postMessage({ type: "error", message: error?.stack ?? String(error) });
  }
});

self.postMessage({ type: "ready" });
