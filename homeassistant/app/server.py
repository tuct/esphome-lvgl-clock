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
import re
import shutil
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

from hass_ws import HassWS, WSError

ROOT = os.environ.get("CC24_ROOT", os.path.dirname(os.path.abspath(__file__)))
CONFIG_DIR = os.environ.get("CC24_CONFIG", "/config")
OPTIONS_PATH = os.environ.get("CC24_OPTIONS", "/data/options.json")
# /data is the add-on's own persistent volume - it survives restarts and
# updates, and unlike /config it is nobody else's business.
DATA_DIR = os.environ.get("CC24_DATA", "/data")
DISPLAYS_PATH = os.path.join(DATA_DIR, "displays.json")
SUPERVISOR_TOKEN = os.environ.get("SUPERVISOR_TOKEN", "")
CORE_API = os.environ.get("CC24_CORE_API", "http://supervisor/core/api")
CORE_WS = os.environ.get("CC24_CORE_WS", "ws://supervisor/core/websocket")
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
#
# TWO PASSES, INSIDE ONE TEMPLATE, and that structure is the point. Emitting
# every text/select/button/sensor in the house overflows Home Assistant's
# 256 kB render limit on any real installation - a house with 33 devices has
# hundreds of sensors alone:
#
#   Error rendering template: Template output exceeded maximum size
#
# So the first pass emits nothing: it only collects the DEVICE ids that look
# like a wall. The second expands just those, through device_entities(). The
# output is a handful of devices however big the house is.
DISCOVER_TEMPLATE = """
{%- set ns = namespace(ids=[]) -%}

{#- a wall by its marker: model is what the thing is -#}
{%- for s in states.text -%}
  {%- set d = device_id(s.entity_id) -%}
  {%- if d and d not in ns.ids
        and (device_attr(d, 'model') or '') | lower == '__MODEL__' -%}
    {%- set ns.ids = ns.ids + [d] -%}
  {%- endif -%}
{%- endfor -%}

{#- and by its shape, so a master flashed before the marker still appears -#}
{%- for s in states.select -%}
  {%- set o = s.attributes.options | default([]) -%}
  {%- if 'time' in o and 'wave' in o -%}
    {%- set d = device_id(s.entity_id) -%}
    {%- if d and d not in ns.ids -%}{%- set ns.ids = ns.ids + [d] -%}{%- endif -%}
  {%- endif -%}
{%- endfor -%}

{%- set out = namespace(rows=[]) -%}
{%- for did in ns.ids[:__MAXDEV__] -%}
  {%- for eid in device_entities(did) -%}
    {%- if eid.split('.')[0] in ['text', 'select', 'button', 'sensor', 'number'] -%}
      {%- set s = states[eid] -%}
      {%- if s -%}
        {%- set out.rows = out.rows + [{
          "entity_id": eid,
          "name": s.name,
          "state": s.state,
          "options": s.attributes.options | default([]),
          "unit": s.attributes.unit_of_measurement | default(''),
          "min": s.attributes.min | default(none),
          "max": s.attributes.max | default(none),
          "step": s.attributes.step | default(none),
          "device_class": s.attributes.device_class | default(''),
          "device_id": did,
          "device": device_attr(did, 'name_by_user') or device_attr(did, 'name'),
          "manufacturer": device_attr(did, 'manufacturer'),
          "model": device_attr(did, 'model'),
          "sw": device_attr(did, 'sw_version')}] -%}
      {%- endif -%}
    {%- endif -%}
  {%- endfor -%}
{%- endfor -%}
{{ out.rows | to_json }}
"""
# str.replace, not %-formatting: Jinja's own {%- ... -%} looks like a format
# spec to Python and blows up before the template is ever sent.
DISCOVER_TEMPLATE = (DISCOVER_TEMPLATE
                     .replace("__MODEL__", MASTER_MODEL)
                     .replace("__MAXDEV__", "12"))


# A pattern slot is "Pattern 1".."Pattern 8" - the name AND the entity_id both
# carry the number. Matching on it is the difference between identifying a slot
# and assuming one: the previous rule treated every text entity on the device
# as a slot, so anything else the master ever grows - a wifi field, a note -
# would have shown up as somewhere to write a pattern.
PATTERN_SLOT_RE = re.compile(r"pattern[ _-]*(\d+)\s*$")


def slot_index(row):
    """Which pattern slot this text entity is, or None if it is not one.

    Both the friendly name and the entity_id are checked. A rename changes the
    name but not the entity_id, and adoption in another language changes
    neither - so between them one of the two almost always still says
    `pattern_3`.
    """
    for candidate in ((row.get("name") or "").lower(), row["entity_id"].lower()):
        m = PATTERN_SLOT_RE.search(candidate.strip())
        if m:
            return int(m.group(1))
    return None


def role_of(row):
    """What a given entity is FOR.

    Classified by the shape of the entity rather than by its name where that is
    possible: the options a select offers are a fact about the firmware, while
    its name is whatever the person who set it up decided to call it, in
    whatever language. `Mode` renamed to `Was die Wand macht` still offers
    `time` and `wave`.
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
        # Text has no shape to go on - a pattern and a cycle list are both just
        # strings, and an empty slot is not even that. So: numbered means slot,
        # cycle/mode means the rotation, and anything else is left alone rather
        # than guessed at.
        # Colours before slots: "Hand colour" has no number in it, so the slot
        # test does not claim it - but say so explicitly rather than relying on
        # that.
        if "colour" in name or "color" in name:
            return "bg_color" if ("background" in name or "bg" in name) else "hand_color"
        if slot_index(row) is not None:
            return "pattern_text"
        if "cycle" in name or "mode" in name:
            return "cycle_modes"
        return None
    if domain == "select" and "opposite" in low and "long" in low:
        return "movement"
    if domain == "number":
        # Named rather than shaped: two sliders in seconds and a multiplier
        # have no structural difference to tell them apart by.
        if "speed" in name:
            return "mode_speed"
        if "transition" in name or "sweep" in name:
            return "transition"
    if domain == "button":
        # Both of the master's buttons say "firmware", and controls is a dict -
        # so matching them the same way silently drops one of them. What each
        # one RESTORES is the difference.
        if "pattern" in name:
            return "reload_patterns"
        if "look" in name or "reset" in name:
            return "reset_look"
    if domain == "sensor":
        # device_class is what Home Assistant itself calls it. Falling back to
        # the unit catches an ESPHome sensor that never declared one.
        if (row.get("device_class") or "").lower() == "temperature":
            return "temperature"
        if (row.get("unit") or "") in ("°C", "°F", "K"):
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
               "unit": r.get("unit") or "",
               "min": r.get("min"), "max": r.get("max"), "step": r.get("step"),
               "chars": len(state) if state not in ("unknown", "unavailable") else 0}
        if role == "pattern_text":
            ent["slot"] = slot_index(r)
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
        d["slots"].sort(key=lambda s: s["slot"])
    out.sort(key=lambda d: (not d["is_master"], d["device"].lower()))
    return out


# Only these. An add-on with access to Core can call anything; this one has no
# business doing more than driving the wall it was written for.
ALLOWED = {("text", "set_value"), ("select", "select_option"), ("button", "press"),
           ("number", "set_value")}


def call(domain, service, data):
    if (domain, service) not in ALLOWED:
        raise ValueError("%s.%s is not something this add-on calls" % (domain, service))
    eid = data.get("entity_id") or ""
    if not eid.startswith(domain + "."):
        raise ValueError("entity_id does not belong to %s" % domain)
    if domain == "number":
        try:
            data["value"] = float(data.get("value"))
        except (TypeError, ValueError):
            raise ValueError("value must be a number")
    if domain == "text":
        value = data.get("value")
        # EMPTY IS VALID. An empty cycle list means "no rotation, stay on
        # whatever Mode says", and an empty pattern slot means "nothing here,
        # fall back to the time". The non-empty check was written for pattern
        # strings and quietly made both of those impossible.
        if not isinstance(value, str):
            raise ValueError("value must be a string")
        # The master's text fields are 255 characters. A packed 24-clock
        # pattern is about 164, so this only trips on something that is not a
        # pattern - and it trips here rather than as a silent truncation on the
        # wall.
        if len(value) > 255:
            raise ValueError("%d characters - the master's limit is 255" % len(value))
    core("/services/%s/%s" % (domain, service), data)
    return {"ok": True}


# ---- saved displays -------------------------------------------------------
# A display is a full-screen view with its own look: which board it follows,
# what it shows, and how it is drawn. Several are the point - a tablet in the
# hall and one on a desk want different sizes and different colours, and
# neither wants to be reconfigured to look at the other.

DISPLAY_FIELDS = {
    "id": str, "name": str, "board": str, "mirror": bool, "mode": str,
    "cycle": str, "cycle_interval": int, "digit_gap": float,
    "mode_speed": float, "transition": int,
    "movement": str, "window": int, "return_to_time": bool,
    "hand_color": str, "background": str, "face_color": str, "show_face": bool,
}
DISPLAY_DEFAULTS = {
    "name": "Display", "board": "", "mirror": True, "mode": "cycle",
    "cycle": "", "cycle_interval": 120, "digit_gap": 0.0,
    "mode_speed": 1.0, "transition": 5000,
    "movement": "opposite", "window": 35, "return_to_time": True,
    "hand_color": "#ffffff", "background": "#000000",
    "face_color": "#1f1f23", "show_face": False,
}


def load_displays():
    try:
        with open(DISPLAYS_PATH, "r", encoding="utf-8") as fh:
            got = json.load(fh)
        return got if isinstance(got, list) else []
    except (OSError, ValueError):
        return []


def save_displays(items):
    """Whole-list write, validated field by field.

    Everything here ends up as attributes on a rendered page, so nothing is
    stored that was not asked for and nothing keeps a type it was not given -
    a colour that is not a colour is the kind of thing that becomes markup.
    """
    if not isinstance(items, list):
        raise ValueError("expected a list of displays")
    if len(items) > 24:
        raise ValueError("24 displays is already more than anyone has screens")
    clean = []
    for i, raw in enumerate(items):
        if not isinstance(raw, dict):
            raise ValueError("display %d is not an object" % i)
        out = dict(DISPLAY_DEFAULTS)
        out["id"] = str(raw.get("id") or "d%d" % (i + 1))[:32]
        for key, kind in DISPLAY_FIELDS.items():
            if key not in raw or key == "id":
                continue
            v = raw[key]
            try:
                out[key] = kind(v) if kind is not bool else bool(v)
            except (TypeError, ValueError):
                raise ValueError("%s: %r is not a %s" % (key, v, kind.__name__))
        for key in ("hand_color", "background", "face_color"):
            if not re.fullmatch(r"#[0-9a-fA-F]{3,8}", out[key]):
                raise ValueError("%s must be a hex colour, got %r" % (key, out[key]))
        out["name"] = out["name"][:60] or "Display"
        out["cycle_interval"] = max(5, min(3600, out["cycle_interval"]))
        out["digit_gap"] = max(0.0, min(2.0, out["digit_gap"]))
        out["mode_speed"] = max(0.1, min(5.0, out["mode_speed"]))
        out["transition"] = max(200, min(60000, out["transition"]))
        out["window"] = max(2, min(3600, out["window"]))
        if out["movement"] not in ("opposite", "clockwise", "counter", "long"):
            raise ValueError("movement: %r is not one of opposite/clockwise/"
                             "counter/long" % out["movement"])
        # One long string of mode names; the card ignores any it does not know.
        out["cycle"] = out["cycle"][:255]
        clean.append(out)
    os.makedirs(DATA_DIR, exist_ok=True)
    tmp = DISPLAYS_PATH + ".tmp"
    with open(tmp, "w", encoding="utf-8") as fh:
        json.dump(clean, fh, indent=1)
    os.replace(tmp, DISPLAYS_PATH)      # atomic: never a half-written list
    return clean


CARD_URL = "/local/" + CARD


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


def resource_state():
    """Is the card registered as a Lovelace resource, and can we do it?

    Resources only exist when Lovelace is in storage mode - the UI-managed
    kind. Under `lovelace: mode: yaml` the registry is not the source of truth
    and the command is refused, which is a thing to SAY rather than a thing to
    retry.
    """
    try:
        with HassWS(CORE_WS, SUPERVISOR_TOKEN) as ws:
            for r in ws.command(type="lovelace/resources") or []:
                if (r.get("url") or "").split("?")[0] == CARD_URL:
                    return {"registered": True, "can_register": True,
                            "resource_id": r.get("id")}
            return {"registered": False, "can_register": True}
    except (WSError, OSError) as err:
        return {"registered": False, "can_register": False, "why": str(err)}


def register_resource():
    """Add /local/<card> to Lovelace's resources, once.

    Listing resources succeeds under `lovelace: mode: yaml` while CREATING one
    is refused, so this cannot be decided up front - the refusal only arrives
    on the attempt. It comes back as can_register:false rather than as an
    error, so the page can show the manual instructions instead of a failure.
    """
    state = resource_state()
    if state.get("registered"):
        return dict(state, already=True)
    try:
        with HassWS(CORE_WS, SUPERVISOR_TOKEN) as ws:
            ws.command(type="lovelace/resources/create", res_type="module",
                       url=CARD_URL)
    except (WSError, OSError) as err:
        return {"registered": False, "can_register": False, "why": str(err)}
    return {"registered": True, "can_register": True}


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

        if tail == "api/displays":
            return self._send(200, json.dumps({"displays": load_displays()}))

        if tail == "api/card":
            path = install_card()
            out = {"installed": bool(path), "file": CARD, "url": CARD_URL,
                   "error": None if path else "run stage.sh, then rebuild the add-on"}
            out.update(resource_state())
            return self._send(200, json.dumps(out))

        # Static, confined to www/ - normalise then verify, so ".." cannot
        # climb out of it.
        return self._file(posixpath.normpath("/" + tail).lstrip("/"))

    def do_POST(self):
        tail = urlparse(self.path).path.strip("/")
        try:
            n = int(self.headers.get("Content-Length") or 0)
            body = json.loads(self.rfile.read(n) or b"{}")
            if tail == "api/displays":
                return self._send(200, json.dumps(
                    {"displays": save_displays(body.get("displays"))}))
            if tail == "api/card/register":
                if not install_card():
                    raise RuntimeError("the bundle is missing - rebuild the add-on")
                return self._send(200, json.dumps(register_resource()))
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
