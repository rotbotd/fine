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

let code = 0;
try {
  code = fine.callMain(["live-lift-probe"]) ?? 0;
} catch (error) {
  if (typeof error?.status === "number")
    code = error.status;
  else
    throw error;
}

if (code !== 0)
  throw new Error(`pthread Fine exited ${code}: ${stderr.join("\n")}`);
for (const expected of [
  "spacer-completed-while-lifter-blocked: true",
  "producer-completed-while-lifter-blocked: true",
  "latest-observed: 11",
  "latest-published: 11",
]) {
  if (!stdout.includes(expected))
    throw new Error(`pthread probe omitted ${expected}: ${stdout.join("\n")}`);
}
if (!(fine.HEAP8?.buffer instanceof SharedArrayBuffer))
  throw new Error("pthread Fine did not expose shared Wasm memory");

console.log("pthread wasm smoke passed with shared memory and two live C++ worker threads");
