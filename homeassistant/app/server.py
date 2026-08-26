#!/usr/bin/env python3
"""Digital Clock Clock 24 - the add-on's web UI.

A control panel for the wall, and the pattern editor that feeds it.

Everything here goes through Home Assistant: the master is an ESPHome node
whose mode, pattern slot, cycle interval and eight pattern strings are all
entities. This add-on finds that node, shows those entities as something nicer
than a list of rows, and writes back to them. It does not talk to the hardware
and it does not need to - the master is already on the network.

Nothing is compiled and nothing is flashed. A pattern is data, not firmware.

Standard library only. This runs on the same Raspberry Pi as everything else in
Home Assistant, and a control panel is not worth a dependency tree.
"""

import json
import os
import posixpath
import shutil
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

ROOT = os.environ.get("CC24_ROOT", os.path.dirname(os.path.abspath(__file__)))
CONFIG_DIR = os.environ.get("CC24_CONFIG", "/config")
OPTIONS_PATH = os.environ.get("CC24_OPTIONS", "/data/options.json")
SUPERVISOR_TOKEN = os.environ.get("SUPERVISOR_TOKEN", "")
CORE_API = os.environ.get("CC24_CORE_API", "http://supervisor/core/api")
PORT = 8099

WWW = os.path.join(ROOT, "www")
CARD = "clockclock24-card.js"

# The marker board_d.yaml carries. Home Assistant splits an ESPHome device's
# project.name on the dot into manufacturer and model, so `tuct.digitalclock-
# clock24` registers as manufacturer `tuct`, model `digitalclockclock24`.
#
# The MODEL is what identifies a wall - it says what the thing is. The
# manufacturer says who wrote it, and would match every other project of
# theirs, so it is not the half to match on.
MASTER_MODEL = "digitalclockclock24"


def load_options():
    defaults = {"install_card": True}
    try:
        with open(OPTIONS_PATH, "r", encoding="utf-8") as fh:
            defaults.update(json.load(fh) or {})
    except (OSError, ValueError):
        pass  # running outside the Supervisor, or first start
    return defaults


# ---- Home Assistant -------------------------------------------------------
# An add-on with homeassistant_api: true reaches Core through the Supervisor
# with its own token. No credentials to configure, and nothing to expose.

def core(path, payload=None, raw=False):
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
            if raw:
                return body.decode()
            return json.loads(body) if body else {}
    except urllib.error.HTTPError as err:
        raise RuntimeError("Home Assistant returned %d: %s"
                           % (err.code, err.read().decode()[:200]))


# The device registry is not in the REST states API, and the WebSocket API
# would mean hand-rolling RFC 6455 framing for one lookup. /api/template
# renders server-side and has device_attr(), which is the whole registry -
# one request, no dependencies.
DISCOVER_TEMPLATE = """
{%- set ns = namespace(rows=[]) -%}
{%- for group in [states.text, states.select, states.button, states.sensor] -%}
{%- for s in group -%}
  {%- set did = device_id(s.entity_id) -%}
  {%- if did -%}
    {%- set ns.rows = ns.rows + [{
      "entity_id": s.entity_id,
      "name": s.name,
      "state": s.state,
      "options": s.attributes.options | default([]),
      "device_id": did,
      "device": device_attr(did, "name_by_user") or device_attr(did, "name"),
      "manufacturer": device_attr(did, "manufacturer"),
      "model": device_attr(did, "model"),
      "sw": device_attr(did, "sw_version")}] -%}
  {%- endif -%}
{%- endfor -%}
{%- endfor -%}
{{ ns.rows | to_json }}
"""


def role_of(row):
    """What a given entity is FOR.

    Classified by the shape of the entity rather than by its name: the options
    a select offers are a fact about the firmware, while its name is whatever
    the person who set it up decided to call it, in whatever language. `Mode`
    renamed to `Was die Wand macht` still offers `time` and `wave`.
    """
    eid = row["entity_id"]
    domain = eid.split(".", 1)[0]
    opts = [str(o) for o in (row.get("options") or [])]
    low = [o.lower() for o in opts]
    name = (row.get("name") or "").lower()

    if domain == "select":
        if "time" in low and "wave" in low:
            return "mode"
        if any(o.endswith(" min") for o in low) or "off" in low:
            return "interval"
        if opts and all(o.isdigit() for o in opts):
            return "pattern_slot"
    if domain == "text":
        # A pattern slot holds "<name>:<base64>"; the cycle list holds mode
        # names separated by commas. Both are text, so the name decides - but
        # only between these two, which is a much smaller thing to get wrong.
        if "cycle" in name or "mode" in name:
            return "cycle_modes"
        return "pattern_text"
    if domain == "button" and ("reload" in name or "firmware" in name):
        return "reload"
    if domain == "sensor" and ("temp" in name or "°c" in (row.get("state") or "")):
        return "temperature"
    return None


def discover():
    """Every board we can drive, with its controls sorted into roles.

    Masters first: a device whose manufacturer is the project marker is a
    ClockClock 24 and nothing else is. A wall flashed before that marker
    existed still appears - it just does not get the badge - because leaving
    somebody with an empty dropdown and no explanation is worse.
    """
    rows = json.loads(core("/template", {"template": DISCOVER_TEMPLATE}, raw=True))

    devices = {}
    for r in rows:
        role = role_of(r)
        if not role:
            continue
        did = r.get("device_id") or "-"
        d = devices.setdefault(did, {
            "device_id": did,
            "device": r.get("device") or "Unknown device",
            "model": r.get("model"),
            "sw": r.get("sw"),
            "is_master": (r.get("model") or "").lower() == MASTER_MODEL,
            "controls": {}, "slots": [], "temperature": None})
        state = r.get("state") or ""
        ent = {"entity_id": r["entity_id"], "name": r.get("name") or r["entity_id"],
               "state": state, "options": [str(o) for o in (r.get("options") or [])],
               "chars": len(state) if state not in ("unknown", "unavailable") else 0}
        if role == "pattern_text":
            d["slots"].append(ent)
        elif role == "temperature":
            d["temperature"] = ent
        else:
            d["controls"][role] = ent

    # A device with a Mode select and pattern slots is a wall. One stray text
    # entity somewhere else in the house is not, and should not be offered.
    out = [d for d in devices.values()
           if d["is_master"] or ("mode" in d["controls"] and d["slots"])]
    for d in out:
        d["slots"].sort(key=lambda s: s["entity_id"])
    out.sort(key=lambda d: (not d["is_master"], d["device"].lower()))
    return out


# Only these. An add-on with access to Core can call anything; this one has no
# business doing more than driving the wall it was written for.
ALLOWED = {("text", "set_value"), ("select", "select_option"), ("button", "press")}


def call(domain, service, data):
    if (domain, service) not in ALLOWED:
        raise ValueError("%s.%s is not something this add-on calls" % (domain, service))
    eid = data.get("entity_id") or ""
    if not eid.startswith(domain + "."):
        raise ValueError("entity_id does not belong to %s" % domain)
    if domain == "text":
        value = data.get("value")
        if not isinstance(value, str) or not value:
            raise ValueError("value must be a non-empty string")
        # The master's text fields are 255 characters. A packed 24-clock
        # pattern is about 164, so this only trips on something that is not a
        # pattern - and it trips here rather than as a silent truncation on the
        # wall.
        if len(value) > 255:
            raise ValueError("%d characters - the master's limit is 255" % len(value))
    core("/services/%s/%s" % (domain, service), data)
    return {"ok": True}


def install_card():
    """Copy the Lovelace bundle into /config/www, so it is at /local/.

    Installing the add-on installs the cards. The alternative is telling people
    to download a file from GitHub and drop it in the right folder, which is a
    step that goes wrong.
    """
    src = os.path.join(WWW, CARD)
    if not os.path.exists(src):
        return None
    try:
        dst_dir = os.path.join(CONFIG_DIR, "www")
        os.makedirs(dst_dir, exist_ok=True)
        dst = os.path.join(dst_dir, CARD)
        if not (os.path.exists(dst) and os.path.getsize(dst) == os.path.getsize(src)):
            shutil.copy2(src, dst)
        return dst
    except OSError as err:
        print("[dcc24] could not install the card: %s" % err, flush=True)
        return None


CTYPES = {".html": "text/html; charset=utf-8", ".js": "text/javascript; charset=utf-8",
          ".css": "text/css; charset=utf-8", ".svg": "image/svg+xml",
          ".png": "image/png", ".json": "application/json"}


class Handler(BaseHTTPRequestHandler):
    server_version = "dcc24/0.3"

    def log_message(self, fmt, *args):
        print("[dcc24] " + fmt % args, flush=True)

    def _send(self, code, body, ctype="application/json"):
        if isinstance(body, str):
            body = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _file(self, rel, ctype=None):
        full = os.path.realpath(os.path.join(WWW, rel))
        if not full.startswith(os.path.realpath(WWW)) or not os.path.isfile(full):
            return self._send(404, json.dumps({"error": "not found"}))
        with open(full, "rb") as fh:
            return self._send(200, fh.read(),
                              ctype or CTYPES.get(os.path.splitext(full)[1],
                                                  "application/octet-stream"))

    def do_GET(self):
        # Ingress publishes the add-on under /api/hassio_ingress/<token>/, but
        # the Supervisor strips that prefix before proxying, so what arrives
        # here is a plain path. The page still has to use RELATIVE urls, since
        # the prefix is what the browser sees.
        url = urlparse(self.path)
        tail = url.path.strip("/")

        # Both pages live in ui/ but are served from the ROOT, so that the
        # relative script paths inside them (sim/, cards/) resolve the same way
        # for both. Serving ui/display.html at its own path would make every
        # one of those a 404.
        if tail in ("", "index.html"):
            return self._file("ui/index.html")
        if tail == "display.html":
            return self._file("ui/display.html")

        if tail == "api/discover":
            try:
                return self._send(200, json.dumps({"devices": discover()}))
            except Exception as err:                    # noqa: BLE001 - shown to the user
                return self._send(200, json.dumps({"devices": [], "error": str(err)}))

        if tail == "api/card":
            path = install_card()
            return self._send(200, json.dumps(
                {"installed": bool(path), "file": CARD,
                 "error": None if path else "run stage.sh, then rebuild the add-on"}))

        # Static, confined to www/ - normalise then verify, so ".." cannot
        # climb out of it.
        return self._file(posixpath.normpath("/" + tail).lstrip("/"))

    def do_POST(self):
        tail = urlparse(self.path).path.strip("/")
        try:
            n = int(self.headers.get("Content-Length") or 0)
            body = json.loads(self.rfile.read(n) or b"{}")
            if tail != "api/call":
                return self._send(404, json.dumps({"error": "not found"}))
            domain = body.get("domain")
            service = body.get("service")
            data = body.get("data") or {}
            result = call(domain, service, data)
            print("[dcc24] %s.%s %s" % (domain, service, data.get("entity_id")), flush=True)
            return self._send(200, json.dumps(result))
        except Exception as err:                        # noqa: BLE001 - shown to the user
            return self._send(400, json.dumps({"error": str(err)}))


if __name__ == "__main__":
    if load_options().get("install_card", True):
        print("[dcc24] card: %s" % (install_card() or "not installed"), flush=True)
    print("[dcc24] serving %s on :%d" % (WWW, PORT), flush=True)
    ThreadingHTTPServer(("0.0.0.0", PORT), Handler).serve_forever()
