import { isolateHistory } from "@codemirror/commands";
import { Transaction } from "@codemirror/state";

export function replaceDocument(view, source) {
  const current = view.state.doc.toString();
  if (current === source)
    return false;
  view.dispatch({
    changes: { from: 0, to: view.state.doc.length, insert: source },
    annotations: [
      Transaction.userEvent.of("input.materialize"),
      isolateHistory.of("full"),
    ],
  });
  return true;
}

export function terminateAndReplace(worker, view, source) {
  worker.terminate();
  return replaceDocument(view, source);
}
