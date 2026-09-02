import { pathToFileURL } from "node:url";
import path from "node:path";

const root = path.resolve(process.argv[2]);
const samplePath = path.resolve(process.argv[3] ?? path.join(root, "sample.fine"));
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

const source = await import("node:fs/promises").then((fs) => fs.readFile(samplePath, "utf8"));
fine.FS.writeFile("/smoke.fine", source);
let code = 0;
try {
  code = fine.callMain(["rain", "--proof-selector", "z3", "/smoke.fine"]) ?? 0;
} catch (error) {
  if (typeof error?.status === "number")
    code = error.status;
  else
    throw error;
}

if (code !== 0)
  throw new Error(`Fine exited ${code}: ${stderr.join("\n")}`);
const events = stdout.map((line) => JSON.parse(line));
for (const operation of ["proof.model.grammar", "proof.model.solve", "proof.model.lift", "proof-core.run.close"])
  if (!events.some((event) => event.operation === operation))
    throw new Error(`missing Rainfall operation: ${operation}`);
console.log(`wasm smoke passed with ${events.length} Rainfall events`);
