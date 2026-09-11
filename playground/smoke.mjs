import { pathToFileURL } from "node:url";
import path from "node:path";
import { readFile } from "node:fs/promises";
import { history, undo } from "@codemirror/commands";
import { EditorState } from "@codemirror/state";
import { replaceDocument, terminateAndReplace } from "./atomic-edit.js";
import { runCheckpointEpoch } from "./checkpoint-epoch.js";
import { selectedProofHoles } from "./rainfall.js";

const root = path.resolve(process.argv[2]);
const samplePath = path.resolve(process.argv[3] ?? path.join(root, "sample.fine"));
const materializePath = process.argv[4] ? path.resolve(process.argv[4]) : null;
const expectedMaterializedPath = process.argv[5] ? path.resolve(process.argv[5]) : null;
const checkpointPath = process.argv[6] ? path.resolve(process.argv[6]) : null;
const partialCheckpointPath = process.argv[7] ? path.resolve(process.argv[7]) : null;
const completeCheckpointPath = process.argv[8] ? path.resolve(process.argv[8]) : null;
const definitionsPath = process.argv[9] ? path.resolve(process.argv[9]) : null;
const specializePath = process.argv[10] ? path.resolve(process.argv[10]) : null;
const expectedSpecializedPath = process.argv[11] ? path.resolve(process.argv[11]) : null;
const expectedDefaultSpecializedPath = process.argv[12] ? path.resolve(process.argv[12]) : null;
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
    || selections[0].body !== "trans(left, middle, right) using [first = p, second = q]")
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
      if (requiresTermination && !terminated)
        throw new Error("checkpoint source changed before its worker was terminated");
      state = state.update(...specs).state;
    },
  };
  let terminated = false;
  let requiresTermination = false;
  view.dispatch({ changes: { from: state.doc.length, insert: "// unsaved prior edit" } });
  const beforeMaterialization = state.doc.toString();
  const worker = { terminate() { terminated = true; } };
  requiresTermination = true;
  if (!terminateAndReplace(worker, view, materialized) || state.doc.toString() !== expected)
    throw new Error("atomic editor replacement did not install the materialized source");
  if (!undo(view) || state.doc.toString() !== beforeMaterialization)
    throw new Error("one undo did not restore the exact pre-materialization bytes");
  if (!undo(view) || state.doc.toString() !== original || undo(view))
    throw new Error("materialization merged with prior editor history or created extra transactions");
}

if (checkpointPath && partialCheckpointPath && completeCheckpointPath) {
  const original = await readFile(checkpointPath, "utf8");
  const expectedPartial = await readFile(partialCheckpointPath, "utf8");
  const expectedComplete = await readFile(completeCheckpointPath, "utf8");
  const invokeEpoch = (args) => {
    stdout.length = 0;
    stderr.length = 0;
    let epochCode = 0;
    try {
      epochCode = fine.callMain(args) ?? 0;
    } catch (error) {
      if (typeof error?.status === "number")
        epochCode = error.status;
      else
        throw error;
    }
    return { code: epochCode, stdout: [...stdout], stderr: [...stderr] };
  };
  const partial = runCheckpointEpoch(fine, invokeEpoch, original, 2, 1);
  const complete = runCheckpointEpoch(fine, invokeEpoch, partial.source, 2, 2);
  const settled = runCheckpointEpoch(fine, invokeEpoch, complete.source, 2, 3);
  if (partial.source !== expectedPartial || complete.source !== expectedComplete
      || settled.source !== complete.source)
    throw new Error("checkpoint epochs did not retain partial, complete, and settled source snapshots exactly");
  for (const [name, epoch, status, hasLift] of [
    ["partial", partial, "checkpointed", true],
    ["complete", complete, "verified", true],
    ["settled", settled, "verified", false],
  ]) {
    const events = epoch.rainfall.map((line) => JSON.parse(line));
    if (events.some((event) => event.operation === "proof.model.lift") !== hasLift
        || events.at(-1)?.operation !== "proof-core.run.close"
        || events.at(-1)?.data?.status !== status)
      throw new Error(`${name} checkpoint did not pair its source with a complete Rainfall trace`);
  }
}

if (definitionsPath) {
  stdout.length = 0;
  stderr.length = 0;
  fine.FS.writeFile("/definitions.fine", await readFile(definitionsPath, "utf8"));
  try {
    code = fine.callMain(["rain", "/definitions.fine"]) ?? 0;
  } catch (error) {
    if (typeof error?.status === "number")
      code = error.status;
    else
      throw error;
  }
  if (code !== 0)
    throw new Error(`definition-only Fine document exited ${code}: ${stderr.join("\n")}`);
  const definitionEvents = stdout.map((line) => JSON.parse(line));
  if (definitionEvents.at(-1)?.operation !== "proof-core.document.close"
      || definitionEvents.at(-1)?.data?.proof_functions_verified !== 2)
    throw new Error("definition-only Wasm document did not close without a fabricated run");
}

if (specializePath && expectedSpecializedPath) {
  stdout.length = 0;
  stderr.length = 0;
  const original = await readFile(specializePath, "utf8");
  const expected = await readFile(expectedSpecializedPath, "utf8");
  fine.FS.writeFile("/specialize.fine", original);
  try {
    code = fine.callMain([
      "specialize", "recover_one", "--output", "/specialized.fine", "/specialize.fine",
    ]) ?? 0;
  } catch (error) {
    if (typeof error?.status === "number")
      code = error.status;
    else
      throw error;
  }
  if (code !== 0)
    throw new Error(`Fine specialize exited ${code}: ${stderr.join("\n")}`);
  const specialized = fine.FS.readFile("/specialized.fine", { encoding: "utf8" });
  if (specialized !== expected)
    throw new Error("Wasm specialization did not preserve the exact expected concrete source");

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
  const beforeSpecialization = state.doc.toString();
  if (!replaceDocument(view, specialized) || state.doc.toString() !== expected)
    throw new Error("atomic editor replacement did not install the specialized source");
  if (!undo(view) || state.doc.toString() !== beforeSpecialization)
    throw new Error("one undo did not restore the exact pre-specialization bytes");
  if (!undo(view) || state.doc.toString() !== original || undo(view))
    throw new Error("specialization merged with prior editor history or created extra transactions");

  if (expectedDefaultSpecializedPath) {
    stdout.length = 0;
    stderr.length = 0;
    fine.FS.writeFile("/default-specialize.fine", source);
    try {
      code = fine.callMain([
        "specialize", "zero_from_one", "--output", "/default-specialized.fine",
        "/default-specialize.fine",
      ]) ?? 0;
    } catch (error) {
      if (typeof error?.status === "number")
        code = error.status;
      else
        throw error;
    }
    if (code !== 0)
      throw new Error(`default Fine specialization exited ${code}: ${stderr.join("\n")}`);
    const defaultSpecialized = fine.FS.readFile("/default-specialized.fine", { encoding: "utf8" });
    if (defaultSpecialized !== await readFile(expectedDefaultSpecializedPath, "utf8"))
      throw new Error("untouched playground source did not produce its exact default specialization");
  }

  stdout.length = 0;
  stderr.length = 0;
  try {
    code = fine.callMain([
      "specialize", "missing_wrapper", "--output", "/failed-specialized.fine", "/specialize.fine",
    ]) ?? 0;
  } catch (error) {
    if (typeof error?.status === "number")
      code = error.status;
    else
      throw error;
  }
  if (code === 0)
    throw new Error("missing specialization target unexpectedly succeeded");
  // Emscripten mirrors the expected CLI failure into Node's eventual process
  // status even though callMain returned control to this multi-call smoke.
  process.exitCode = 0;
  try {
    fine.FS.readFile("/failed-specialized.fine");
    throw new Error("failed specialization created source output");
  } catch (error) {
    if (error.message === "failed specialization created source output")
      throw error;
  }
}

console.log(`wasm smoke passed with ${events.length} Rainfall events, atomic materialization/specialization, and paired checkpoint epochs`);
