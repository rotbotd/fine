import { basicSetup, EditorView } from "codemirror";
import { defaultHighlightStyle, StreamLanguage, syntaxHighlighting } from "@codemirror/language";
import { Compartment } from "@codemirror/state";
import { tags } from "@lezer/highlight";
import { compressedWasm, createFine, ordinaryWasm, pthreadCapable } from "./generated-assets.js";
import { replaceDocument, terminateAndReplace } from "./atomic-edit.js";
import { selectedProofHoles } from "./rainfall.js";

const sourceHost = document.querySelector("#source");
const run = document.querySelector("#run");
const materialize = document.querySelector("#materialize");
const checkpoint = document.querySelector("#checkpoint");
const stopCheckpoint = document.querySelector("#stop-checkpoint");
const checkpointBudget = document.querySelector("#checkpoint-budget");
const status = document.querySelector("#status");
const result = document.querySelector("#result");
const rainfall = document.querySelector("#rainfall");

let capture = null;
let nextInput = 0;
let checkpointWorker = null;
let lastCheckpoint = null;
let completedEpochs = 0;

const fineLanguage = StreamLanguage.define({
  tokenTable: {
    definition: tags.function(tags.definition(tags.variableName)),
    hole: tags.atom,
    proofKeyword: tags.modifier,
    proofTerm: tags.constant(tags.name),
    proofType: tags.className,
    valueType: tags.typeName,
  },
  startState() {
    return {
      expectsDefinition: false,
      afterProof: false,
      definitionKind: null,
      valueTypes: new Set(["Int", "Bool"]),
      proofTypes: new Set(["Id"]),
    };
  },
  copyState(state) {
    return {
      ...state,
      valueTypes: new Set(state.valueTypes),
      proofTypes: new Set(state.proofTypes),
    };
  },
  token(stream, state) {
    if (stream.eatSpace())
      return null;
    if (stream.match("//")) {
      stream.skipToEnd();
      return "lineComment";
    }
    if (stream.match(/^[0-9]+/))
      return "number";
    if (stream.match(/^(?:==|!=|<=|>=|->|=>|<-|&&|\|\||[=<>+*/!-])/))
      return "operator";
    if (stream.match(/^[()[\]{},;:.]/))
      return "punctuation";
    if (stream.match("?"))
      return "hole";
    if (stream.match(/^[A-Za-z_][A-Za-z0-9_]*/)) {
      const word = stream.current();
      if (word === "enum") {
        state.expectsDefinition = true;
        state.definitionKind = "valueType";
        state.afterProof = false;
        return "definitionKeyword";
      }
      if (word === "function") {
        state.expectsDefinition = true;
        state.definitionKind = null;
        state.afterProof = false;
        return "definitionKeyword";
      }
      if (["run", "let"].includes(word)) {
        state.expectsDefinition = true;
        state.definitionKind = null;
        state.afterProof = false;
        return "definitionKeyword";
      }
      if (word === "proof") {
        state.afterProof = true;
        return "proofKeyword";
      }
      if (word === "inductive" && state.afterProof) {
        state.afterProof = false;
        state.expectsDefinition = true;
        state.definitionKind = "proofType";
        return "proofKeyword";
      }
      if (state.afterProof) {
        state.afterProof = false;
        state.expectsDefinition = false;
        return "definition";
      }
      if (state.expectsDefinition) {
        state.expectsDefinition = false;
        if (state.definitionKind === "valueType")
          state.valueTypes.add(word);
        if (state.definitionKind === "proofType")
          state.proofTypes.add(word);
        state.definitionKind = null;
        return "definition";
      }
      if (["takes", "inducts", "ensures", "using", "assert", "match"].includes(word))
        return "keyword";
      if (state.valueTypes.has(word))
        return "valueType";
      if (state.proofTypes.has(word))
        return "proofType";
      if (["true", "false"].includes(word))
        return "bool";
      if (word === "refl")
        return "proofTerm";
      if (word === "result")
        return "selfName";
      return "variableName";
    }
    stream.next();
    return null;
  },
});

let useCompressedWasm = false;
try {
  const probe = await fetch("./zstd-check.txt.zst", { cache: "no-store" });
  useCompressedWasm = probe.ok && await probe.text() === "fine-zstd-ok";
} catch {
  // Browsers without HTTP Zstandard support use the ordinary Wasm response.
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
document.documentElement.dataset.fineRuntime = pthreadCapable ? "pthreads" : "single-threaded";

const sample = await fetch("./sample.fine").then((response) => response.text());
const editing = new Compartment();
const editor = new EditorView({
  doc: sample,
  extensions: [
    basicSetup,
    fineLanguage,
    syntaxHighlighting(defaultHighlightStyle, { fallback: true }),
    EditorView.lineWrapping,
    editing.of(EditorView.editable.of(true)),
  ],
  parent: sourceHost,
});

function invoke(args) {
  capture = { stdout: [], stderr: [] };
  let code = 0;
  try {
    code = fine.callMain(args) ?? 0;
  } catch (error) {
    if (typeof error?.status === "number") {
      code = error.status;
    } else {
      capture = null;
      throw error;
    }
  }
  const completed = capture;
  capture = null;
  completed.code = code;
  return completed;
}

function showRainfall(lines) {
  return lines.map((line) => {
    try {
      const event = JSON.parse(line);
      const heading = `${event.sequence ?? "?"}  ${event.operation ?? "event"}`;
      return `${heading}\n${JSON.stringify(event.data ?? {}, null, 2)}`;
    } catch {
      return line;
    }
  }).join("\n\n");
}

async function execute() {
  run.disabled = true;
  materialize.disabled = true;
  checkpoint.disabled = true;
  checkpointBudget.disabled = true;
  status.textContent = "running…";
  result.textContent = "";
  rainfall.textContent = "";

  const path = `/playground-${nextInput++}.fine`;
  fine.FS.writeFile(path, editor.state.doc.toString());
  try {
    const ordinary = invoke(["run", "--proof-selector", "z3", path]);
    const diagnostics = [...ordinary.stdout, ...ordinary.stderr].join("\n") || `(exit ${ordinary.code})`;
    result.textContent = diagnostics;
    if (ordinary.code !== 0) {
      status.textContent = "failed";
      return;
    }

    const rain = invoke(["rain", "--proof-selector", "z3", path]);
    const selections = selectedProofHoles(rain.stdout);
    const filled = selections.length > 0
      ? selections.map(({ binding, body }) => `${binding} ← ${body}`).join("\n")
      : "(none)";
    result.textContent = `filled holes\n${filled}\n\nverification\n${diagnostics}`;
    rainfall.textContent = showRainfall(rain.stdout);
    if (rain.stderr.length > 0)
      rainfall.textContent += `${rainfall.textContent ? "\n\n" : ""}${rain.stderr.join("\n")}`;
    status.textContent = rain.code === 0 ? "verified" : "rainfall failed";
  } catch (error) {
    result.textContent = error?.stack ?? String(error);
    status.textContent = "crashed";
  } finally {
    fine.FS.unlink(path);
    run.disabled = false;
    materialize.disabled = false;
    checkpoint.disabled = false;
    checkpointBudget.disabled = false;
  }
}

async function materializeSource() {
  run.disabled = true;
  materialize.disabled = true;
  checkpoint.disabled = true;
  checkpointBudget.disabled = true;
  status.textContent = "materializing…";
  result.textContent = "";
  rainfall.textContent = "";

  const inputPath = `/playground-${nextInput++}.fine`;
  const outputPath = `/playground-${nextInput++}-materialized.fine`;
  fine.FS.writeFile(inputPath, editor.state.doc.toString());
  try {
    const completed = invoke([
      "materialize", "--proof-selector", "z3", "--output", outputPath, inputPath,
    ]);
    if (completed.code !== 0) {
      result.textContent = [...completed.stdout, ...completed.stderr].join("\n")
        || `(exit ${completed.code})`;
      status.textContent = "failed";
      return;
    }

    const source = fine.FS.readFile(outputPath, { encoding: "utf8" });
    const changed = replaceDocument(editor, source);
    result.textContent = changed
      ? "materialized source\nall proof edits committed as one undoable editor transaction"
      : "source already contains the selected proof terms";
    status.textContent = changed ? "materialized" : "unchanged";
  } catch (error) {
    result.textContent = error?.stack ?? String(error);
    status.textContent = "crashed";
  } finally {
    fine.FS.unlink(inputPath);
    try {
      fine.FS.unlink(outputPath);
    } catch {
      // A failed materialization does not create the output file.
    }
    run.disabled = false;
    materialize.disabled = false;
    checkpoint.disabled = false;
    checkpointBudget.disabled = false;
  }
}

function resetCheckpointControls() {
  editor.dispatch({ effects: editing.reconfigure(EditorView.editable.of(true)) });
  checkpointWorker = null;
  lastCheckpoint = null;
  completedEpochs = 0;
  run.disabled = false;
  materialize.disabled = false;
  checkpoint.disabled = false;
  checkpointBudget.disabled = false;
  stopCheckpoint.disabled = true;
}

function installLastCheckpoint(label) {
  const source = lastCheckpoint;
  const epochs = completedEpochs;
  const worker = checkpointWorker;
  const changed = terminateAndReplace(worker, editor, source);
  resetCheckpointControls();
  result.textContent = changed
    ? `${label}\ninstalled epoch ${epochs} as one undoable editor transaction`
    : `${label}\nno completed epoch changed the source`;
  status.textContent = changed ? "checkpointed" : "unchanged";
}

function beginCheckpointSearch() {
  const budget = Number.parseInt(checkpointBudget.value, 10);
  if (!Number.isInteger(budget) || budget < 1) {
    status.textContent = "budget must be positive";
    return;
  }

  run.disabled = true;
  materialize.disabled = true;
  checkpoint.disabled = true;
  checkpointBudget.disabled = true;
  stopCheckpoint.disabled = false;
  editor.dispatch({ effects: editing.reconfigure(EditorView.editable.of(false)) });
  status.textContent = "starting checkpoint worker…";
  result.textContent = "no completed epoch yet";
  rainfall.textContent = "";
  lastCheckpoint = editor.state.doc.toString();
  completedEpochs = 0;
  checkpointWorker = new Worker(new URL("./checkpoint-worker.js", import.meta.url), { type: "module" });

  checkpointWorker.addEventListener("message", ({ data }) => {
    if (!checkpointWorker)
      return;
    if (data?.type === "ready") {
      checkpointWorker.postMessage({
        type: "start",
        source: lastCheckpoint,
        budget,
        maxEpochs: 64,
      });
      status.textContent = "searching checkpoint epochs…";
      return;
    }
    if (data?.type === "epoch") {
      lastCheckpoint = data.source;
      completedEpochs = data.epoch;
      rainfall.textContent = showRainfall(data.rainfall);
      result.textContent = `completed checkpoint epoch ${completedEpochs}\nthe editor is unchanged until stop`;
      return;
    }
    if (data?.type === "done") {
      lastCheckpoint = data.source;
      completedEpochs = data.epoch;
      rainfall.textContent = showRainfall(data.rainfall);
      installLastCheckpoint("checkpoint search settled");
      return;
    }
    if (data?.type === "limit") {
      lastCheckpoint = data.source;
      completedEpochs = data.epoch;
      installLastCheckpoint("checkpoint epoch limit reached");
      return;
    }
    if (data?.type === "error") {
      checkpointWorker.terminate();
      result.textContent = data.message;
      status.textContent = "checkpoint failed";
      resetCheckpointControls();
    }
  });
  checkpointWorker.addEventListener("error", (error) => {
    if (!checkpointWorker)
      return;
    checkpointWorker.terminate();
    result.textContent = error.message || "checkpoint worker crashed";
    status.textContent = "checkpoint crashed";
    resetCheckpointControls();
  });
}

function interruptCheckpointSearch() {
  if (!checkpointWorker)
    return;
  installLastCheckpoint("checkpoint search interrupted");
}

run.disabled = false;
materialize.disabled = false;
checkpoint.disabled = false;
checkpointBudget.disabled = false;
stopCheckpoint.disabled = true;
status.textContent = "ready";
run.addEventListener("click", execute);
materialize.addEventListener("click", materializeSource);
checkpoint.addEventListener("click", beginCheckpointSearch);
stopCheckpoint.addEventListener("click", interruptCheckpointSearch);
