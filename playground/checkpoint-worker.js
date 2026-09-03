import { compressedWasm, createFine, ordinaryWasm, pthreadCapable } from "./generated-assets.js";
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
    if (pthreadCapable) {
      const inputPath = "/live-checkpoint.fine";
      const outputPath = "/live-checkpoint-materialized.fine";
      const rainfallPath = "/live-checkpoint.rain";
      fine.FS.writeFile(inputPath, source);
      try {
        const completed = invoke([
          "live-checkpoint", "--proof-start", String(data.budget), "--output", outputPath,
          "--rain-output", rainfallPath, inputPath,
        ]);
        if (completed.code !== 0) {
          const diagnostics = [...completed.stdout, ...completed.stderr].join("\n")
            || `(exit ${completed.code})`;
          throw new Error(diagnostics);
        }
        source = fine.FS.readFile(outputPath, { encoding: "utf8" });
        const rainfallText = fine.FS.readFile(rainfallPath, { encoding: "utf8" });
        self.postMessage({
          type: "done",
          epoch: "live",
          source,
          rainfall: rainfallText.split("\n").filter((line) => line.length > 0),
        });
      } finally {
        for (const path of [inputPath, outputPath, rainfallPath]) {
          try {
            fine.FS.unlink(path);
          } catch {
            // An interrupted or failed live run may not publish output files.
          }
        }
      }
      return;
    }
    for (let epoch = 1; epoch <= data.maxEpochs; ++epoch) {
      const checkpoint = runCheckpointEpoch(fine, invoke, source, data.budget, epoch);
      if (checkpoint.source === source) {
        self.postMessage({ type: "done", epoch: epoch - 1, source, rainfall: checkpoint.rainfall });
        return;
      }
      source = checkpoint.source;
      self.postMessage({ type: "epoch", epoch, source, rainfall: checkpoint.rainfall });
      await new Promise((resolve) => setTimeout(resolve, 0));
    }
    self.postMessage({ type: "limit", epoch: data.maxEpochs, source });
  } catch (error) {
    self.postMessage({ type: "error", message: error?.stack ?? String(error) });
  }
});

let live = null;
if (pthreadCapable) {
  const slots = fine._fine_live_mailbox_slot_count();
  live = {
    memory: fine.HEAP8.buffer,
    latest: fine._fine_live_mailbox_latest_address(),
    payloadCapacity: fine._fine_live_mailbox_payload_capacity(),
    sequences: Array.from({ length: slots }, (_, slot) =>
      fine._fine_live_mailbox_sequence_address(slot)),
    lengths: Array.from({ length: slots }, (_, slot) =>
      fine._fine_live_mailbox_length_address(slot)),
    payloads: Array.from({ length: slots }, (_, slot) =>
      fine._fine_live_mailbox_payload_address(slot)),
  };
}
self.postMessage({ type: "ready", live });
