import createFine from "./fine.mjs";

const source = document.querySelector("#source");
const run = document.querySelector("#run");
const status = document.querySelector("#status");
const result = document.querySelector("#result");
const rainfall = document.querySelector("#rainfall");

let capture = null;
let nextInput = 0;

const fine = await createFine({
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
  fine.FS.writeFile(path, source.value);
  try {
    const ordinary = invoke(["run", "--proof-selector", "z3", path]);
    result.textContent = [...ordinary.stdout, ...ordinary.stderr].join("\n") || `(exit ${ordinary.code})`;
    if (ordinary.code !== 0) {
      status.textContent = "failed";
      return;
    }

    const rain = invoke(["rain", "--proof-selector", "z3", path]);
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

source.value = await fetch("./sample.fine").then((response) => response.text());
source.disabled = false;
run.disabled = false;
status.textContent = "ready";
run.addEventListener("click", execute);
