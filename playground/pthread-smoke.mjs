import { pathToFileURL } from "node:url";
import path from "node:path";

const root = path.resolve(process.argv[2]);
const createFine = (await import(pathToFileURL(path.join(root, "fine.mjs")))).default;
const stdout = [];
const stderr = [];
const fine = await createFine({
  locateFile(file) {
    return path.join(root, file);
  },
  print(line) {
    stdout.push(line);
  },
  printErr(line) {
    stderr.push(line);
  },
});

function invoke(args) {
  stdout.length = 0;
  stderr.length = 0;
  let code = 0;
  try {
    code = fine.callMain(args) ?? 0;
  } catch (error) {
    if (typeof error?.status === "number")
      code = error.status;
    else
      throw error;
  }
  return { code, stdout: [...stdout], stderr: [...stderr] };
}

const probe = invoke(["live-lift-probe"]);
if (probe.code !== 0)
  throw new Error(`pthread Fine exited ${probe.code}: ${probe.stderr.join("\n")}`);
for (const expected of [
  "spacer-completed-while-lifter-blocked: true",
  "producer-completed-while-lifter-blocked: true",
  "latest-observed: 11",
  "latest-published: 11",
]) {
  if (!probe.stdout.includes(expected))
    throw new Error(`pthread probe omitted ${expected}: ${probe.stdout.join("\n")}`);
}
if (!(fine.HEAP8?.buffer instanceof SharedArrayBuffer))
  throw new Error("pthread Fine did not expose shared Wasm memory");

const checkpointFixture = process.argv[3];
const checkpointSource = await (await import("node:fs/promises")).readFile(checkpointFixture, "utf8");
fine.FS.writeFile("/live-input.fine", checkpointSource);
const live = invoke([
  "live-checkpoint", "--proof-limit", "2", "--output", "/live-output.fine", "/live-input.fine",
]);
if (live.code !== 0)
  throw new Error(`pthread live checkpoint exited ${live.code}: ${live.stderr.join("\n")}`);

const slots = fine._fine_live_mailbox_slot_count();
const latestAddress = fine._fine_live_mailbox_latest_address();
const latest = Atomics.load(new Uint32Array(fine.HEAP8.buffer, latestAddress, 1), 0) >>> 0;
if (latest === 0xffffffff)
  throw new Error("pthread live checkpoint published no mailbox view");
const published = [];
const first = latest >= slots ? latest - slots + 1 : 0;
for (let sequence = first; sequence <= latest; ++sequence) {
  const slot = sequence % slots;
  const sequenceAddress = fine._fine_live_mailbox_sequence_address(slot);
  if ((Atomics.load(new Uint32Array(fine.HEAP8.buffer, sequenceAddress, 1), 0) >>> 0) !== sequence)
    continue;
  const length = Atomics.load(new Uint32Array(
    fine.HEAP8.buffer, fine._fine_live_mailbox_length_address(slot), 1,
  ), 0) >>> 0;
  const payload = new Uint8Array(
    fine.HEAP8.buffer, fine._fine_live_mailbox_payload_address(slot), length,
  ).slice();
  published.push(JSON.parse(new TextDecoder().decode(payload)));
}
if (published.length < 2 || published.at(-1).budget !== 2
    || published.at(-1).body !== "trans(left, middle, right) using [first = p, second = ?]")
  throw new Error(`pthread live mailbox omitted its ordered partial views: ${JSON.stringify(published)}`);
const materialized = fine.FS.readFile("/live-output.fine", { encoding: "utf8" });
if (published.at(-1).source !== materialized)
  throw new Error("last pthread mailbox source differs from the command checkpoint");
fine.FS.writeFile("/live-validation.fine", materialized);
const validation = invoke(["validate-checkpoint", "/live-validation.fine"]);
if (validation.code !== 0)
  throw new Error(`live mailbox source did not reparse and recheck: ${validation.stderr.join("\n")}`);

console.log("pthread wasm smoke passed with shared memory, live source views, and two C++ worker threads");
