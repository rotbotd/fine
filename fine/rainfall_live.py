"""Local browser editor for the atomic Rainfall host protocol."""

from __future__ import annotations

import json
import os
import threading
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import urlsplit

from rainfall_host import (HostError, advance, initialize, load_state, locked,
                           run_generation)


MAX_SOURCE_BYTES = 2 * 1024 * 1024


class LiveError(ValueError):
    pass


def minimal_edit(old: str, new: str) -> list[dict[str, Any]]:
    """Return one UTF-8 byte edit spanning exactly the changed middle."""
    if old == new:
        return []
    prefix = 0
    limit = min(len(old), len(new))
    while prefix < limit and old[prefix] == new[prefix]:
        prefix += 1
    suffix = 0
    old_left, new_left = len(old) - prefix, len(new) - prefix
    while (suffix < old_left and suffix < new_left and
           old[len(old) - suffix - 1] == new[len(new) - suffix - 1]):
        suffix += 1
    old_end = len(old) - suffix if suffix else len(old)
    new_end = len(new) - suffix if suffix else len(new)
    return [{
        "from": len(old[:prefix].encode("utf-8")),
        "to": len(old[:old_end].encode("utf-8")),
        "insert": new[prefix:new_end],
    }]


class LiveSession:
    def __init__(self, host: Path, fine: Path):
        self.host = host
        self.fine = fine
        self._edit_lock = threading.Lock()
        self._workers: set[threading.Thread] = set()
        self._workers_lock = threading.Lock()

    def state(self) -> dict[str, Any]:
        with locked(self.host):
            return load_state(self.host)

    def start_generation(self, generation: str) -> None:
        def work() -> None:
            try:
                run_generation(self.host, self.fine, generation)
            finally:
                with self._workers_lock:
                    self._workers.discard(threading.current_thread())

        worker = threading.Thread(target=work, daemon=True,
                                  name=f"fine-rain-{generation.rsplit(':', 1)[-1]}")
        with self._workers_lock:
            self._workers.add(worker)
        worker.start()

    def replace_source(self, source: str) -> dict[str, Any]:
        encoded = source.encode("utf-8")
        if len(encoded) > MAX_SOURCE_BYTES:
            raise LiveError(f"source exceeds {MAX_SOURCE_BYTES} bytes")
        with self._edit_lock:
            state = self.state()
            edits = minimal_edit(state["display_source"], source)
            if not edits:
                return {
                    "schema": "fine.rainfall.live-action.v1",
                    "status": "unchanged",
                    "generation": state["current_generation"],
                    "display_snapshot": state["display_snapshot"],
                }
            action = advance(self.host, edits)
            self.start_generation(action["generation"])
            return action


INDEX_HTML = r'''<!doctype html>
<html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width">
<title>Fine Rainfall live</title>
<style>
:root{color-scheme:dark;--bg:#131313;--panel:#1b1b1b;--line:#343434;--text:#dedbd3;--muted:#8d8a82;--current:#75c98a;--transported:#e3ae59;--unplaced:#e36d6d}
*{box-sizing:border-box}html,body{height:100%;margin:0}body{background:var(--bg);color:var(--text);font:13px ui-monospace,SFMono-Regular,Consolas,monospace;display:grid;grid-template-rows:38px 1fr}
header{display:flex;align-items:center;gap:14px;padding:0 12px;border-bottom:1px solid var(--line);background:#171717}#title{font-weight:700}#snapshot{color:var(--muted)}#run{margin-left:auto;padding:3px 7px;border:1px solid var(--line);border-radius:3px}.admitted{color:var(--current)}.requested,.superseded{color:var(--transported)}.failed,.discarded{color:var(--unplaced)}
main{min-height:0;display:grid;grid-template-columns:minmax(26rem,1.15fr) minmax(22rem,.85fr)}#source{width:100%;height:100%;resize:none;border:0;border-right:1px solid var(--line);outline:0;background:#151515;color:var(--text);padding:16px;font:14px/1.55 ui-monospace,SFMono-Regular,Consolas,monospace;tab-size:4}
#evidence{min-width:0;overflow:auto;background:var(--panel)}#banner{position:sticky;top:0;z-index:2;padding:10px 12px;border-bottom:1px solid var(--line);background:#202020}#banner.transported{color:var(--transported);border-bottom-style:dashed}#banner.failed{color:var(--unplaced)}#cards{padding:8px}.card{padding:10px 8px;border-bottom:1px solid var(--line)}.cardhead{display:flex;gap:10px;align-items:baseline}.state{font-weight:700}.state.current{color:var(--current)}.state.transported{color:var(--transported)}.state.unplaced{color:var(--unplaced)}.loc{color:var(--muted)}.excerpt{margin:8px 0;padding:7px;background:#151515;white-space:pre-wrap;max-height:9rem;overflow:auto}.role{margin-top:6px}.count{color:var(--muted)}details{margin-top:6px}summary{cursor:pointer}pre{white-space:pre-wrap;max-height:16rem;overflow:auto;color:#bbb}.empty{padding:18px;color:var(--muted)}
@media(max-width:760px){main{grid-template-columns:1fr;grid-template-rows:55% 45%}#source{border-right:0;border-bottom:1px solid var(--line)}}
</style>
<header><span id="title">fine rainfall</span><span id="snapshot">loading</span><span id="run">connecting</span></header>
<main><textarea id="source" spellcheck="false" aria-label="Fine source"></textarea><section id="evidence"><div id="banner">waiting for host state</div><div id="cards"></div></section></main>
<script>
const editor=document.querySelector('#source'), snapshot=document.querySelector('#snapshot'), run=document.querySelector('#run'), banner=document.querySelector('#banner'), cards=document.querySelector('#cards');
let initialized=false,timer=null,queue=Promise.resolve();
const esc=s=>String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
function grouped(items){const m=new Map;for(const a of items){const k=a.claim.source;if(!m.has(k))m.set(k,[]);m.get(k).push(a)}return [...m.values()]}
function render(state){const rev=state.display_snapshot.revision,current=state.current_generation,record=state.generations[current],status=record.status; snapshot.textContent=`${state.document.display_name} · revision ${rev}`;run.textContent=status;run.className=status;
 const counts={current:0,transported:0,unplaced:0};for(const a of state.annotations)counts[a.status]++;
 if(status==='failed'){banner.textContent=record.failure?.error||'fine failed';banner.className='failed'}else if(status==='admitted'){banner.textContent=`current evidence · ${counts.current} markers admitted for revision ${rev}`;banner.className='current'}else{banner.textContent=`solver running · ${counts.transported} transported and ${counts.unplaced} unplaced markers are visibly stale`;banner.className='transported'}
 if(!state.annotations.length){cards.innerHTML='<div class="empty">no admitted evidence for this display yet</div>';return}
 const bytes=new TextEncoder().encode(state.display_source);cards.innerHTML=grouped(state.annotations).map(group=>{const a=group[0],sp=a.display_span,loc=sp?`${sp.begin.line}:${sp.begin.column}–${sp.end.line}:${sp.end.column}`:'unplaced';let shown='';if(sp){shown=new TextDecoder().decode(bytes.slice(sp.begin.offset,sp.end.offset))}
 const acts=group.filter(x=>x.activity).map(x=>{const ac=x.activity,n=ac.accepted_instances.length,ad=ac.accepted_instances.filter(i=>i.admitted_clause_event).length;const detail=ac.accepted_instances.map(i=>`<details><summary>${esc(i.accepted_event)} · ${i.admitted_clause_event?'admitted':'not admitted'}</summary><pre>${esc(i.instance_text)}</pre></details>`).join('');return `<div class="role"><b>${esc(ac.role)}</b> <span class="count">${n} accepted, ${ad} admitted</span>${detail}</div>`}).join('');
 return `<article class="card"><div class="cardhead"><span class="state ${a.status}">${a.status}</span><span class="loc">${esc(loc)}</span></div><div class="excerpt">${esc(shown)}</div>${acts||`<details><summary>${group.length} evidence edge${group.length===1?'':'s'}</summary><pre>${esc(group.map(x=>x.term_text).join('\n\n'))}</pre></details>`}</article>`}).join('')}
async function poll(){try{const r=await fetch('/api/state',{cache:'no-store'});if(!r.ok)throw Error(await r.text());const s=await r.json();if(!initialized){editor.value=s.display_source;initialized=true}render(s)}catch(e){run.textContent='disconnected';run.className='failed';banner.textContent=String(e);banner.className='failed'}}
function submit(value){queue=queue.then(async()=>{const r=await fetch('/api/source',{method:'POST',headers:{'content-type':'application/json'},body:JSON.stringify({source:value})});if(!r.ok)throw Error(await r.text())}).catch(e=>{banner.textContent=String(e);banner.className='failed'});return queue}
editor.addEventListener('input',()=>{clearTimeout(timer);timer=setTimeout(()=>submit(editor.value),220)});poll();setInterval(poll,300);
</script></html>'''


def make_handler(session: LiveSession) -> type[BaseHTTPRequestHandler]:
    class Handler(BaseHTTPRequestHandler):
        server_version = "FineRainfall/1"

        def _send(self, status: HTTPStatus, body: bytes, content_type: str) -> None:
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.send_header("Content-Security-Policy",
                             "default-src 'self'; script-src 'unsafe-inline'; "
                             "style-src 'unsafe-inline'; connect-src 'self'; "
                             "frame-ancestors 'none'")
            self.end_headers()
            self.wfile.write(body)

        def _json(self, status: HTTPStatus, value: Any) -> None:
            self._send(status, (json.dumps(value, separators=(",", ":")) + "\n").encode(),
                       "application/json; charset=utf-8")

        def do_GET(self) -> None:  # noqa: N802
            path = urlsplit(self.path).path
            try:
                if path == "/":
                    self._send(HTTPStatus.OK, INDEX_HTML.encode(), "text/html; charset=utf-8")
                elif path == "/api/state":
                    self._json(HTTPStatus.OK, session.state())
                else:
                    self._json(HTTPStatus.NOT_FOUND, {"error": "not found"})
            except (OSError, HostError) as error:
                self._json(HTTPStatus.CONFLICT, {"error": str(error)})

        def do_POST(self) -> None:  # noqa: N802
            if urlsplit(self.path).path != "/api/source":
                self._json(HTTPStatus.NOT_FOUND, {"error": "not found"})
                return
            try:
                media_type = self.headers.get("Content-Type", "").split(";", 1)[0].lower()
                if media_type != "application/json":
                    self._json(HTTPStatus.UNSUPPORTED_MEDIA_TYPE,
                               {"error": "content type must be application/json"})
                    return
                origin = self.headers.get("Origin")
                if origin and urlsplit(origin).hostname not in {"127.0.0.1", "localhost", "::1"}:
                    self._json(HTTPStatus.FORBIDDEN, {"error": "cross-origin edit rejected"})
                    return
                try:
                    length = int(self.headers.get("Content-Length", "0"))
                except ValueError as error:
                    raise LiveError("content length is malformed") from error
                if length <= 0 or length > MAX_SOURCE_BYTES * 2:
                    raise LiveError("request body has an invalid size")
                value = json.loads(self.rfile.read(length))
                if not isinstance(value, dict) or not isinstance(value.get("source"), str):
                    raise LiveError("request must be a JSON object with a source string")
                self._json(HTTPStatus.ACCEPTED, session.replace_source(value["source"]))
            except (UnicodeError, json.JSONDecodeError, LiveError) as error:
                self._json(HTTPStatus.BAD_REQUEST, {"error": str(error)})
            except (OSError, HostError) as error:
                self._json(HTTPStatus.CONFLICT, {"error": str(error)})

        def log_message(self, format: str, *args: Any) -> None:
            return

    return Handler


class LiveServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True


def initialize_session(host: Path, source: Path, fine: Path, document: str | None,
                       resume: bool) -> LiveSession:
    if not fine.is_file() or not os.access(fine, os.X_OK):
        raise LiveError(f"Fine executable is not runnable: {fine}")
    state_path = host / "state.json"
    if state_path.exists():
        if not resume:
            raise LiveError(f"host is already initialized: {host}; pass --resume")
    else:
        initialize(host, source.read_bytes(), str(source), document)
    session = LiveSession(host, fine)
    state = session.state()
    record = state["generations"][state["current_generation"]]
    if record["status"] in {"requested", "superseded"}:
        session.start_generation(state["current_generation"])
    return session
