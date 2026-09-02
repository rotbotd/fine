export function selectedProofHoles(lines) {
  const bindings = new Map();
  const selections = new Map();

  for (const line of lines) {
    let event;
    try {
      event = JSON.parse(line);
    } catch {
      continue;
    }

    const data = event.data ?? {};
    if (event.operation === "proof.search.open"
        && typeof data.id === "string"
        && typeof data.binding === "string") {
      bindings.set(data.id, data.binding);
    }

    if (event.operation === "proof.search.select"
        && typeof data.hole === "string"
        && typeof data.body === "string") {
      selections.set(data.hole, {
        binding: bindings.get(data.hole) ?? data.hole,
        body: data.body,
      });
    }
  }

  return [...selections.values()];
}
