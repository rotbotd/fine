import { pathToFileURL } from "node:url";
import path from "node:path";
import { readFile } from "node:fs/promises";
import { history, undo } from "@codemirror/commands";
import { EditorState } from "@codemirror/state";
import { replaceDocument } from "./atomic-edit.js";
import { selectedProofHoles } from "./rainfall.js";

const root = path.resolve(process.argv[2]);
const samplePath = path.resolve(process.argv[3] ?? path.join(root, "sample.fine"));
const materializePath = process.argv[4] ? path.resolve(process.argv[4]) : null;
const expectedMaterializedPath = process.argv[5] ? path.resolve(process.argv[5]) : null;
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
const selections = selectedProofHoles(stdout);
if (selections.length !== 1
    || selections[0].binding !== "composed"
    || selections[0].body !== "trans[left, middle, right](p, q)")
  throw new Error(`unexpected selected proof holes: ${JSON.stringify(selections)}`);

if (materializePath && expectedMaterializedPath) {
  stdout.length = 0;
  stderr.length = 0;
  const original = await readFile(materializePath, "utf8");
  const expected = await readFile(expectedMaterializedPath, "utf8");
  fine.FS.writeFile("/materialize.fine", original);
  try {
    code = fine.callMain([
      "materialize", "--proof-selector", "z3", "--output", "/materialized.fine", "/materialize.fine",
    ]) ?? 0;
  } catch (error) {
    if (typeof error?.status === "number")
      code = error.status;
    else
      throw error;
  }
  if (code !== 0)
    throw new Error(`Fine materialize exited ${code}: ${stderr.join("\n")}`);
  const materialized = fine.FS.readFile("/materialized.fine", { encoding: "utf8" });
  if (materialized !== expected)
    throw new Error("Wasm materialization did not preserve the exact expected concrete source");

  let state = EditorState.create({ doc: original, extensions: [history()] });
  const view = {
    get state() {
      return state;
    },
    dispatch(...specs) {
      state = state.update(...specs).state;
    },
  };
  view.dispatch({ changes: { from: state.doc.length, insert: "// unsaved prior edit" } });
  const beforeMaterialization = state.doc.toString();
  if (!replaceDocument(view, materialized) || state.doc.toString() !== expected)
    throw new Error("atomic editor replacement did not install the materialized source");
  if (!undo(view) || state.doc.toString() !== beforeMaterialization)
    throw new Error("one undo did not restore the exact pre-materialization bytes");
  if (!undo(view) || state.doc.toString() !== original || undo(view))
    throw new Error("materialization merged with prior editor history or created extra transactions");
}

console.log(`wasm smoke passed with ${events.length} Rainfall events and atomic materialization`);
