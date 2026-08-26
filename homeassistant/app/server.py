#!/usr/bin/env python3
"""Digital Clock Clock 24 - the add-on's web UI.

The pattern editor, in the sidebar, wired straight to the wall.

You draw a pattern, it becomes one line of text, and that line is written into
a Pattern text entity on the master. The master saves it to flash and pushes it
down the sync bus, and about a second later 24 real analogue clocks are running
it. Nothing is compiled and nothing is flashed - that is the point of the
pattern format: it is data, not firmware.

Standard library only. This runs on the same Raspberry Pi as everything else in
Home Assistant, and a drawing tool is not worth a dependency tree.
"""

import json
import os
import posixpath
import shutil
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse

ROOT = os.environ.get("CC24_ROOT", os.path.dirname(os.path.abspath(__file__)))
CONFIG_DIR = os.environ.get("CC24_CONFIG", "/config")
OPTIONS_PATH = os.environ.get("CC24_OPTIONS", "/data/options.json")
SUPERVISOR_TOKEN = os.environ.get("SUPERVISOR_TOKEN", "")
CORE_API = os.environ.get("CC24_CORE_API", "http://supervisor/core/api")
PORT = 8099

WWW = os.path.join(ROOT, "www")
CARD = "clockclock24-card.js"


def load_options():
    defaults = {"install_card": True, "entity_filter": "pattern"}
    try:
        with open(OPTIONS_PATH, "r", encoding="utf-8") as fh:
            defaults.update(json.load(fh) or {})
    except (OSError, ValueError):
        pass  # running outside the Supervisor, or first start
    return defaults


# ---- Home Assistant -------------------------------------------------------
# An add-on with homeassistant_api: true reaches Core through the Supervisor
# with its own token. No credentials to configure, and nothing to expose.

def core(path, payload=None):
    if not SUPERVISOR_TOKEN:
        raise RuntimeError("no SUPERVISOR_TOKEN - not running as an add-on")
    req = urllib.request.Request(
        CORE_API + path,
        data=json.dumps(payload).encode() if payload is not None else None,
        headers={"Authorization": "Bearer " + SUPERVISOR_TOKEN,
                 "Content-Type": "application/json"},
        method="POST" if payload is not None else "GET")
    try:
        with urllib.request.urlopen(req, timeout=10) as r:
            body = r.read()
            return json.loads(body) if body else {}
    except urllib.error.HTTPError as err:
        raise RuntimeError("Home Assistant returned %d: %s"
                           % (err.code, err.read().decode()[:200]))


def pattern_entities(needle):
    """Every text entity that could hold a pattern.

    A pattern slot on the master is a `text.` entity, so that is the filter -
    narrowed by a substring, because a Home Assistant with other integrations
    has plenty of text entities that are not this. Slots are listed even when
    empty: an empty slot is exactly where you want to put a new pattern.
    """
    out = []
    for st in core("/states"):
        eid = st.get("entity_id", "")
        if not eid.startswith("text."):
            continue
        name = (st.get("attributes") or {}).get("friendly_name") or eid
        if needle and needle.lower() not in (eid + " " + name).lower():
            continue
        state = st.get("state") or ""
        out.append({"entity_id": eid, "name": name, "state": state,
                    "chars": len(state) if state not in ("unknown", "unavailable") else 0})
    out.sort(key=lambda e: e["entity_id"])
    return out


def install_card():
    """Copy the Lovelace bundle into /config/www, so it is at /local/.

    Installing the add-on installs the cards. The alternative is telling people
    to download a file from GitHub and drop it in the right folder, which is a
    step that goes wrong.
    """
    src = os.path.join(WWW, CARD)
    if not os.path.exists(src):
        return None
    dst_dir = os.path.join(CONFIG_DIR, "www")
    try:
        os.makedirs(dst_dir, exist_ok=True)
        dst = os.path.join(dst_dir, CARD)
        if os.path.exists(dst) and os.path.getsize(dst) == os.path.getsize(src):
            return dst
        shutil.copy2(src, dst)
        return dst
    except OSError as err:
        print("[dcc24] could not install the card: %s" % err, flush=True)
        return None


# ---- the page -------------------------------------------------------------
# The editor is the CARD, not a copy of it. Same file that runs in Lovelace,
# handed a small `hass` shim that forwards to this server. One editor in the
# repo - fix it once and it is fixed in both places.

INDEX = """<!doctype html>
<title>Digital Clock Clock 24</title>
<style>
 body{font:15px/1.55 system-ui,sans-serif;margin:0;background:#111417;color:#e7e9ee}
 header{padding:16px 22px;border-bottom:1px solid #262a31;display:flex;gap:16px;
   align-items:baseline;flex-wrap:wrap}
 h1{margin:0;font-size:17px} header p{margin:0;color:#9aa1ad;font-size:13px}
 main{padding:20px;max-width:64rem;margin:0 auto}
 .card{background:#181b20;border:1px solid #262a31;border-radius:10px;margin-bottom:18px;
   overflow:hidden}
 .bar{display:flex;gap:10px;align-items:center;flex-wrap:wrap;
   padding:12px 16px;border-bottom:1px solid #262a31}
 .bar label{color:#9aa1ad;font-size:.88em}
 select,input{font:inherit;background:#0e1013;color:#e7e9ee;border:1px solid #2a2f37;
   border-radius:6px;padding:5px 8px}
 select{min-width:20rem}
 .note{color:#9aa1ad;font-size:.85em;padding:0 16px 14px;margin:0}
 .note b{color:#e7e9ee}
 code{background:#0e1013;border:1px solid #262a31;border-radius:4px;padding:1px 5px;
   font-size:.9em}
 pre{background:#0e1013;border:1px solid #262a31;border-radius:8px;padding:10px 12px;
   overflow:auto;font-size:12px;margin:0 16px 14px}
 .warn{color:#e0b070}
</style>
<header>
  <h1>Digital Clock Clock 24</h1>
  <p>Draw a pattern. Send it. Twenty-four clocks are running it a second later.</p>
</header>
<main>
  <div class="card">
    <div class="bar">
      <label for="ent">Pattern slot on the master</label>
      <select id="ent"><option value="">loading…</option></select>
      <label for="pname">Name</label>
      <input id="pname" value="pattern" size="12">
    </div>
    <div id="host"></div>
    <p class="note">
      <b>Send to wall</b> writes the string into that slot. The master saves it
      to flash and pushes it to all seven listeners &mdash; nothing is
      recompiled and nothing is reflashed.
      <span class="warn">It overwrites whatever was in the slot.</span>
      The master's <b>Reload patterns from firmware</b> button is the way back
      to the <code>patterns/</code> folder.
    </p>
  </div>

  <div class="card">
    <div class="bar"><label>The same editor, on your dashboard</label></div>
    <p class="note" id="cardnote">…</p>
    <pre id="yaml">type: custom:clockclock24-editor-card
entity: text.cc24_board_d_pattern_1</pre>
  </div>
</main>

<script src="sim/engine.js"></script>
<script src="sim/wall.js"></script>
<script src="sim/pattern.js"></script>
<script src="cards/editor-card.js"></script>
<script>
// A `hass` shim: the editor card only ever reads states[entity].state and
// calls text.set_value, so this is the whole of the surface it needs.
const hass = {
  states: {},
  async callService(domain, service, data) {
    const r = await fetch("api/set", {
      method: "POST", headers: {"Content-Type": "application/json"},
      body: JSON.stringify(data)
    });
    if (!r.ok) throw new Error((await r.json()).error || "failed");
    hass.states[data.entity_id] = { state: data.value };
  }
};

const host = document.getElementById("host");
const sel = document.getElementById("ent");
const pname = document.getElementById("pname");
let card = null;

function mount() {
  if (card) card.remove();
  card = document.createElement("clockclock24-editor-card");
  card.setConfig({ entity: sel.value || null, name: pname.value || "pattern" });
  host.appendChild(card);
  card.hass = hass;
}

sel.onchange = mount;
pname.onchange = mount;

(async () => {
  try {
    const list = await (await fetch("api/entities")).json();
    if (list.error) throw new Error(list.error);
    sel.innerHTML = list.length
      ? list.map(e => `<option value="${e.entity_id}">${e.name} — ${e.chars || "empty"}${e.chars ? " chars" : ""}</option>`).join("")
      : `<option value="">no text entities found</option>`;
    list.forEach(e => { hass.states[e.entity_id] = { state: e.state }; });
  } catch (err) {
    sel.innerHTML = `<option value="">unavailable — ${err.message}</option>`;
  }
  mount();
  const n = document.getElementById("cardnote");
  const r = await (await fetch("api/card")).json();
  n.innerHTML = r.installed
    ? `Installed at <code>/local/${r.file}</code>. Register it once under
       <b>Settings &rarr; Dashboards &rarr; ⋮ &rarr; Resources &rarr; Add</b>,
       URL <code>/local/${r.file}</code>, type <b>JavaScript module</b>. Then:`
    : `Not installed &mdash; ${r.error || "turn on <code>install_card</code> in the add-on options"}.`;
})();
</script>
"""


class Handler(BaseHTTPRequestHandler):
    server_version = "dcc24/0.2"

    def log_message(self, fmt, *args):
        print("[dcc24] " + fmt % args, flush=True)

    def _send(self, code, body, ctype="application/json"):
        if isinstance(body, str):
            body = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        # Ingress publishes the add-on under /api/hassio_ingress/<token>/, but
        # the Supervisor strips that prefix before proxying, so what arrives
        # here is a plain path. The page still has to use RELATIVE urls, since
        # the prefix is what the browser sees.
        tail = urlparse(self.path).path.strip("/")

        if tail in ("", "index.html"):
            return self._send(200, INDEX, "text/html; charset=utf-8")

        if tail == "api/entities":
            try:
                return self._send(200, json.dumps(
                    pattern_entities(load_options().get("entity_filter", ""))))
            except Exception as err:                    # noqa: BLE001 - shown to the user
                return self._send(200, json.dumps({"error": str(err)}))

        if tail == "api/card":
            path = install_card()
            return self._send(200, json.dumps(
                {"installed": bool(path), "file": CARD,
                 "error": None if path else "run stage.sh, then rebuild the add-on"}))

        # Static, confined to www/ - join then verify, so ".." cannot climb out.
        safe = posixpath.normpath("/" + tail).lstrip("/")
        full = os.path.realpath(os.path.join(WWW, safe))
        if not full.startswith(os.path.realpath(WWW)) or not os.path.isfile(full):
            return self._send(404, json.dumps({"error": "not found"}))
        ctype = {".html": "text/html; charset=utf-8",
                 ".js": "text/javascript; charset=utf-8",
                 ".css": "text/css",
                 ".png": "image/png"}.get(os.path.splitext(full)[1],
                                          "application/octet-stream")
        with open(full, "rb") as fh:
            return self._send(200, fh.read(), ctype)

    def do_POST(self):
        if not urlparse(self.path).path.strip("/") == "api/set":
            return self._send(404, json.dumps({"error": "not found"}))
        try:
            n = int(self.headers.get("Content-Length") or 0)
            body = json.loads(self.rfile.read(n) or b"{}")
            eid, value = body.get("entity_id"), body.get("value")
            if not eid or not eid.startswith("text."):
                raise ValueError("entity_id must be a text. entity")
            if not isinstance(value, str) or not value:
                raise ValueError("value must be a non-empty pattern string")
            # The master's text fields are 255 characters. A packed 24-clock
            # pattern is about 164, so this only trips on something that is not
            # a pattern - and it trips here rather than as a silent truncation
            # on the wall.
            if len(value) > 255:
                raise ValueError("%d characters - the master's limit is 255" % len(value))
            core("/services/text/set_value", {"entity_id": eid, "value": value})
            print("[dcc24] wrote %d chars to %s" % (len(value), eid), flush=True)
            return self._send(200, json.dumps({"ok": True, "chars": len(value)}))
        except Exception as err:                        # noqa: BLE001 - shown to the user
            return self._send(400, json.dumps({"error": str(err)}))


if __name__ == "__main__":
    if load_options().get("install_card", True):
        installed = install_card()
        print("[dcc24] card: %s" % (installed or "not installed"), flush=True)
    print("[dcc24] serving %s on :%d" % (WWW, PORT), flush=True)
    ThreadingHTTPServer(("0.0.0.0", PORT), Handler).serve_forever()
