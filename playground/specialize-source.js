export function specializeDocument(fine, invoke, {
  source,
  functionName,
  inputPath,
  outputPath,
}) {
  fine.FS.writeFile(inputPath, source);
  try {
    const completed = invoke([
      "specialize", functionName, "--output", outputPath, inputPath,
    ]);
    if (completed.code !== 0) {
      let producedOutput = false;
      try {
        fine.FS.stat(outputPath);
        producedOutput = true;
      } catch {
        // The expected failure boundary has no output path.
      }
      if (producedOutput)
        throw new Error("Fine specialization produced source after a failed check");
      return { completed, source: null };
    }
    return {
      completed,
      source: fine.FS.readFile(outputPath, { encoding: "utf8" }),
    };
  } finally {
    fine.FS.unlink(inputPath);
    try {
      fine.FS.unlink(outputPath);
    } catch {
      // The CLI writes its output only after specialization fully succeeds.
    }
  }
}
