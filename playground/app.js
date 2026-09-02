import { basicSetup, EditorView } from "codemirror";
import { defaultHighlightStyle, StreamLanguage, syntaxHighlighting } from "@codemirror/language";
import { tags } from "@lezer/highlight";
import { compressedWasm, createFine, ordinaryWasm } from "./generated-assets.js";
import { selectedProofHoles } from "./rainfall.js";

const sourceHost = document.querySelector("#source");
const run = document.querySelector("#run");
const status = document.querySelector("#status");
const result = document.querySelector("#result");
const rainfall = document.querySelector("#rainfall");

let capture = null;
let nextInput = 0;

const fineLanguage = StreamLanguage.define({
  tokenTable: {
    definition: tags.function(tags.definition(tags.variableName)),
    hole: tags.atom,
    proofKeyword: tags.modifier,
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
      if (["needs", "ensures", "using", "assert", "match"].includes(word))
        return "keyword";
      if (state.valueTypes.has(word))
        return "valueType";
      if (state.proofTypes.has(word))
        return "proofType";
      if (["true", "false"].includes(word))
        return "bool";
      if (word === "refl")
        return "standard";
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

const sample = await fetch("./sample.fine").then((response) => response.text());
const editor = new EditorView({
  doc: sample,
  extensions: [
    basicSetup,
    fineLanguage,
    syntaxHighlighting(defaultHighlightStyle, { fallback: true }),
    EditorView.lineWrapping,
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
  }
}

run.disabled = false;
status.textContent = "ready";
run.addEventListener("click", execute);
