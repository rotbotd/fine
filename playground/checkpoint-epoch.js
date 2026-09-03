export function runCheckpointEpoch(fine, invoke, source, budget, epoch) {
  const inputPath = `/checkpoint-${epoch}.fine`;
  const outputPath = `/checkpoint-${epoch}-materialized.fine`;
  fine.FS.writeFile(inputPath, source);
  try {
    const completed = invoke([
      "checkpoint", "--proof-budget", String(budget), "--output", outputPath, inputPath,
    ]);
    if (completed.code !== 0) {
      const diagnostics = [...completed.stdout, ...completed.stderr].join("\n")
        || `(exit ${completed.code})`;
      throw new Error(diagnostics);
    }
    return fine.FS.readFile(outputPath, { encoding: "utf8" });
  } finally {
    fine.FS.unlink(inputPath);
    try {
      fine.FS.unlink(outputPath);
    } catch {
      // A failed epoch does not publish a checkpoint file.
    }
  }
}
