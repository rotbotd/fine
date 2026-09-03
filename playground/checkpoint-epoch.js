export function runCheckpointEpoch(fine, invoke, source, budget, epoch) {
  const inputPath = `/checkpoint-${epoch}.fine`;
  const outputPath = `/checkpoint-${epoch}-materialized.fine`;
  const rainfallPath = `/checkpoint-${epoch}.rain`;
  fine.FS.writeFile(inputPath, source);
  try {
    const completed = invoke([
      "checkpoint", "--proof-budget", String(budget), "--output", outputPath,
      "--rain-output", rainfallPath, inputPath,
    ]);
    if (completed.code !== 0) {
      const diagnostics = [...completed.stdout, ...completed.stderr].join("\n")
        || `(exit ${completed.code})`;
      throw new Error(diagnostics);
    }
    const nextSource = fine.FS.readFile(outputPath, { encoding: "utf8" });
    const rainfallText = fine.FS.readFile(rainfallPath, { encoding: "utf8" });
    return {
      source: nextSource,
      rainfall: rainfallText.split("\n").filter((line) => line.length > 0),
    };
  } finally {
    fine.FS.unlink(inputPath);
    try {
      fine.FS.unlink(outputPath);
    } catch {
      // A failed epoch does not publish a checkpoint file.
    }
    try {
      fine.FS.unlink(rainfallPath);
    } catch {
      // Source and Rainfall are published only after both files exist.
    }
  }
}
