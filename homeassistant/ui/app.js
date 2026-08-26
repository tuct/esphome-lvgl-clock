// Digital Clock Clock 24 - the add-on's control panel.
//
// Everything on this page is a Home Assistant entity on the master. The panel
// renders them as chips instead of rows, and writes back through one endpoint.
// The pattern editor is the LOVELACE CARD, not a copy of it - handed a small
// `hass` shim, so there is one editor in the repo and fixing it fixes both.

(() => {
  const $ = id => document.getElementById(id);
  const api = (p, body) => fetch(p, body ? {
    method: "POST", headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body)
  } : undefined).then(async r => {
    const j = await r.json().catch(() => ({}));
    if (!r.ok) throw new Error(j.error || `HTTP ${r.status}`);
    return j;
  });

  const call = (domain, service, data) => api("api/call", { domain, service, data });

  let devices = [];
  let board = null;         // the selected device
  // undefined = nobody has chosen yet, so pick a master. "" = the user chose
  // none, and a poll three seconds later must not quietly undo that.
  let chosen;
  let card = null;
  let preview = null, previewKey = "";
  let editing = false;      // suspend polling-driven redraws while typing
  let poll = null;

  // ---- shared hass shim for the editor card ------------------------------
  // The card reads states[entity].state and calls text.set_value. That is the
  // entire surface, so this is the entire shim.
  const hass = {
    states: {},
    async callService(domain, service, data) {
      await call(domain, service, data);
      if (domain === "text") hass.states[data.entity_id] = { state: data.value };
      refresh();
    }
  };

  // ---- status ------------------------------------------------------------
  function setLive(state, message) {
    $("live").className = "dot " + (state || "");
    const b = $("banner");
    if (message) { b.innerHTML = message; b.classList.remove("hidden"); }
    else b.classList.add("hidden");
  }

  // ---- chips -------------------------------------------------------------
  function chips(host, options, current, onPick, label) {
    host.innerHTML = "";
    options.forEach(opt => {
      const value = typeof opt === "string" ? opt : opt.value;
      const b = document.createElement("button");
      b.className = "chip";
      b.type = "button";
      b.setAttribute("aria-pressed", String(value === current));
      b.innerHTML = typeof opt === "string" ? esc(opt) : opt.html;
      if (label) b.title = `${label}: ${value}`;
      b.onclick = async () => {
        if (b.getAttribute("aria-pressed") === "true") return;
        host.querySelectorAll(".chip").forEach(c => c.classList.add("busy"));
        try { await onPick(value); }
        catch (err) { setLive("err", `<b>Failed.</b> ${esc(err.message)}`); }
        finally { host.querySelectorAll(".chip").forEach(c => c.classList.remove("busy")); }
        refresh();
      };
      host.appendChild(b);
    });
  }

  // ---- sliders -----------------------------------------------------------
  // Bound to a number entity: bounds and step come from Home Assistant rather
  // than being duplicated here, so changing them in the YAML changes them here.
  // The write happens on `change`, not `input` - one call when the thumb is
  // released, not forty on the way there.
  let dragging = null;
  function slider(id, ent, fmt) {
    const el = $(id), out = $(id + "v");
    $("f-" + (id === "speed" ? "speed" : "transition")).classList.toggle("hidden", !ent);
    if (!ent) return;
    el.min = ent.min != null ? ent.min : 0;
    el.max = ent.max != null ? ent.max : 100;
    el.step = ent.step != null ? ent.step : 1;
    // Never yank the thumb out from under a finger mid-drag.
    if (dragging !== id) {
      el.value = ent.state;
      out.textContent = fmt(ent.state);
    }
    el.oninput = () => { dragging = id; out.textContent = fmt(el.value); };
    el.onchange = async () => {
      try {
        await call("number", "set_value", { entity_id: ent.entity_id, value: Number(el.value) });
      } catch (err) { setLive("err", `<b>Failed.</b> ${esc(err.message)}`); }
      dragging = null;
      refresh();
    };
  }

  const esc = s => String(s).replace(/[&<>"]/g, c =>
    ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));

  // Mode names are snake_case in the firmware because they are also the wire
  // format. Reading them is nicer than typing them.
  const pretty = m => m.replace(/_/g, " ");

  // ---- render ------------------------------------------------------------
  function render() {
    const c = board ? board.controls : {};
    $("badge").classList.toggle("hidden", !board || !board.is_master);
    // The Wall panel stays visible whatever happens - it carries the board
    // picker, and a page that hides the only way to choose a board when no
    // board is chosen is a page you cannot recover from.
    $("control").classList.toggle("nodevice", !board);
    $("editorpanel").classList.toggle("hidden", !board);

    if (!board) {
      // Stop the animation loops rather than leaving them running behind a
      // hidden panel, burning a frame budget nobody is looking at.
      if (card) { card.remove(); card = null; }
      if (preview) { preview.remove(); preview = null; previewKey = ""; }
      $("preview").classList.add("empty");
      $("wallhint").textContent = "";
      return;
    }
    syncPreview();
    $("wallhint").textContent = [board.model, board.sw].filter(Boolean).join(" · ");

    // Mode
    $("f-mode").classList.toggle("hidden", !c.mode);
    if (c.mode) {
      chips($("modes"), c.mode.options.map(o => ({ value: o, html: esc(pretty(o)) })),
        c.mode.state,
        v => call("select", "select_option", { entity_id: c.mode.entity_id, option: v }),
        "mode");
    }

    // Which pattern slot `pattern` draws - labelled with the pattern's own
    // name where there is one, because "3" tells you nothing.
    $("f-slot").classList.toggle("hidden", !c.pattern_slot);
    if (c.pattern_slot) {
      chips($("slotpick"), c.pattern_slot.options.map(o => {
        const slot = board.slots[Number(o) - 1];
        const name = slot && slot.state ? slot.state.split(":")[0] : "";
        return { value: o, html: `<span class="n">${esc(o)}</span>${name ? " · " + esc(name) : ""}` };
      }), c.pattern_slot.state,
        v => call("select", "select_option", { entity_id: c.pattern_slot.entity_id, option: v }),
        "pattern");
    }

    // Cycle interval
    $("f-interval").classList.toggle("hidden", !c.interval);
    if (c.interval) {
      chips($("intervals"), c.interval.options, c.interval.state,
        v => call("select", "select_option", { entity_id: c.interval.entity_id, option: v }),
        "cycle");
    }

    // Movement
    $("f-movement").classList.toggle("hidden", !c.movement);
    if (c.movement) {
      chips($("movements"), c.movement.options.map(o => ({ value: o, html: esc(pretty(o)) })),
        c.movement.state,
        v => call("select", "select_option", { entity_id: c.movement.entity_id, option: v }),
        "movement");
    }

    // The two sliders
    slider("transition", c.transition, v => `${v} s`);
    slider("speed", c.mode_speed, v => `×${Number(v).toFixed(1)}`);

    // Colours. `change`, not `input` - a colour picker fires continuously while
    // you drag around the wheel, and each one of those is a packet to the wall.
    const colorField = (id, ent) => {
      const el = $(id);
      if (!ent) return false;
      if (document.activeElement !== el) el.value = ent.state || "#000000";
      el.onchange = async () => {
        try { await call("text", "set_value", { entity_id: ent.entity_id, value: el.value }); }
        catch (err) { setLive("err", `<b>Failed.</b> ${esc(err.message)}`); }
        refresh();
      };
      return true;
    };
    const anyColor = colorField("handcolor", c.hand_color) |
                     colorField("bgcolor", c.bg_color);
    $("f-colors").classList.toggle("hidden", !anyColor);
    $("colornote").textContent = anyColor
      ? "an automation can warm the wall at sunset" : "";

    // Cycle list
    $("f-cycle").classList.toggle("hidden", !c.cycle_modes);
    if (c.cycle_modes && !editing) {
      const list = (c.cycle_modes.state || "").split(",").map(s => s.trim()).filter(Boolean);
      const now = c.mode ? c.mode.state : null;
      $("cycletags").innerHTML = list.length
        ? list.map((m, i) => `<span class="tag${m === now ? " now" : ""}">` +
            `<span class="i">${i + 1}</span>${esc(pretty(m))}</span>`).join("")
        : `<span class="hint">empty — the wall stays on one mode</span>`;
      $("cycleinput").value = list.join(",");
    }

    $("reload").classList.toggle("hidden", !c.reload);
    // Only when a sensor is there AND has a reading. `unknown` and
    // `unavailable` are states too, and "Room Temperature: unknown" is worse
    // than showing nothing.
    const tp = board.temperature;
    const has = tp && tp.state && !["unknown", "unavailable", ""].includes(tp.state);
    $("temp").textContent = has ? `${tp.name}: ${tp.state}${tp.unit ? " " + tp.unit : ""}` : "";

    // Editor slot picker
    const slot = $("slot");
    const keep = slot.value;
    slot.innerHTML = board.slots.map(s => {
      const name = s.state ? s.state.split(":")[0] : "";
      return `<option value="${esc(s.entity_id)}">${esc(s.name)}${
        name ? ` — ${esc(name)}` : " — empty"}</option>`;
    }).join("");
    if (board.slots.some(s => s.entity_id === keep)) slot.value = keep;
    board.slots.forEach(s => { hass.states[s.entity_id] = { state: s.state }; });

    const ey = $("editoryaml");
    if (ey && board.slots.length)
      ey.textContent = "type: custom:clockclock24-editor-card\nentity: " + board.slots[0].entity_id;

    if (!card || card.dataset.entity !== slot.value) mountEditor();
    else if (card) card.hass = hass;
  }

  // ---- preview -----------------------------------------------------------
  // The clock card, showing what the wall is showing. Rebuilt only when the
  // mode or the pattern actually changes: setConfig restarts the animation, so
  // rebuilding it on every three-second poll would make it stutter forever.
  function syncPreview() {
    const c = board.controls;
    const mode = c.mode ? c.mode.state : "time";
    const slotNo = c.pattern_slot ? Number(c.pattern_slot.state) : 0;
    const slot = slotNo ? board.slots.find(s => s.slot === slotNo) : null;
    const pattern = mode === "pattern" && slot && slot.state ? slot.state : null;
    const key = mode + "|" + (pattern || "");
    if (key === previewKey && preview) return;
    previewKey = key;

    const cfg = { type: "custom:clockclock24-card", mode, digit_gap: 0 };
    if (pattern) cfg.pattern = pattern;
    // `pattern` with an empty slot has nothing to draw; the card throws rather
    // than showing a blank, so fall back to the clock.
    if (mode === "pattern" && !pattern) cfg.mode = "time";
    const next = document.createElement("clockclock24-card");
    try { next.setConfig(cfg); }
    catch (err) { previewKey = ""; return; }
    if (preview) preview.remove();
    preview = next;
    $("preview").classList.remove("empty");
    $("preview").appendChild(preview);
  }

  function mountEditor() {
    if (card) card.remove();
    card = document.createElement("clockclock24-editor-card");
    card.dataset.entity = $("slot").value;
    card.setConfig({ entity: $("slot").value || null, name: $("pname").value || "pattern" });
    $("host").appendChild(card);
    card.hass = hass;
  }

  // ---- data --------------------------------------------------------------
  async function refresh() {
    try {
      const r = await api("api/discover");
      if (r.error) throw new Error(r.error);
      devices = r.devices || [];
      setLive("on");

      const sel = $("board");
      sel.innerHTML = `<option value="">${devices.length ? "— none —" : "nothing found"}</option>`
        + devices.map(d => `<option value="${esc(d.device_id)}">${esc(d.device)}${
            d.is_master ? "" : " (no marker)"}</option>`).join("");
      if (chosen === undefined)
        chosen = (devices.find(d => d.is_master) || devices[0] || {}).device_id || "";
      sel.value = devices.some(d => d.device_id === chosen) ? chosen : "";
      board = devices.find(d => d.device_id === sel.value) || null;

      if (!devices.length) {
        setLive("err",
          "<b>No wall found.</b> The master needs to be on the network and adopted " +
          "in Home Assistant. If it was flashed before the <code>project:</code> " +
          "marker was added to <code>board_d.yaml</code>, reflash it — that is what " +
          "identifies it here.");
      }
      render();
    } catch (err) {
      // Say what actually failed. "Unreachable" was wrong for a template that
      // rendered fine and then got rejected, and sent people looking at their
      // network instead of at the message.
      setLive("err", `<b>Could not read Home Assistant.</b> ${esc(err.message)}`);
    }
  }

  // ---- wiring ------------------------------------------------------------
  $("board").onchange = () => {
    chosen = $("board").value;
    board = devices.find(d => d.device_id === chosen) || null;
    render();
  };
  $("slot").onchange = mountEditor;
  $("pname").onchange = mountEditor;

  $("cycleedit").onclick = () => {
    editing = true;
    $("cycleeditrow").classList.remove("hidden");
    $("cycleinput").focus();
  };
  $("cyclecancel").onclick = () => {
    editing = false;
    $("cycleeditrow").classList.add("hidden");
    render();
  };
  $("cyclesave").onclick = async () => {
    const c = board.controls.cycle_modes;
    try {
      await call("text", "set_value",
                 { entity_id: c.entity_id, value: $("cycleinput").value.trim() });
    } catch (err) { setLive("err", `<b>Failed.</b> ${esc(err.message)}`); }
    editing = false;
    $("cycleeditrow").classList.add("hidden");
    // The master drops names it does not know and republishes what it kept, so
    // what comes back is the answer - not what was typed.
    setTimeout(refresh, 400);
  };
  $("reload").onclick = async () => {
    const c = board.controls.reload;
    if (!confirm("Discard patterns written from Home Assistant and go back to " +
                 "the patterns/ folder as compiled in?")) return;
    try { await call("button", "press", { entity_id: c.entity_id }); }
    catch (err) { setLive("err", `<b>Failed.</b> ${esc(err.message)}`); }
    setTimeout(refresh, 600);
  };

  // The Mode select on the master republishes every second, so the wall moving
  // itself on shows up here without anyone touching anything.
  function startPolling() {
    clearInterval(poll);
    poll = setInterval(() => { if (!document.hidden) refresh(); }, 3000);
  }
  document.addEventListener("visibilitychange", () => { if (!document.hidden) refresh(); });

  // Copy buttons on the dashboard recipes. A YAML snippet you have to select
  // by hand in a scrolling <pre> is a snippet people mistype.
  document.querySelectorAll(".recipe .copy").forEach(btn => {
    btn.onclick = async () => {
      const text = btn.parentElement.querySelector("pre").textContent;
      try { await navigator.clipboard.writeText(text); }
      catch { return; }               // Ingress is https, but be graceful
      btn.textContent = "Copied";
      btn.classList.add("done");
      setTimeout(() => { btn.textContent = "Copy"; btn.classList.remove("done"); }, 1400);
    };
  });

  // ---- displays ----------------------------------------------------------
  // Each one is a full-screen page with its own look and its own link. Saved
  // in the add-on's /data, so a tablet keeps pointing at the same thing across
  // restarts and updates.
  let displays = [];

  const MODE_CHOICES = ["cycle", "time", "rotate_left", "flying_birds", "wave", "spiral",
                        "wind", "rotating_maze", "zipper", "mirror_wave", "love", "pattern"];

  function displayUrl(d) {
    return "display.html?d=" + encodeURIComponent(d.id);
  }

  function renderDisplays() {
    const host = $("displays");
    if (!displays.length) {
      host.innerHTML = `<p class="empty-note">No displays yet. <b>Add display</b> makes a
        full-screen page with its own link — point a tablet at it and leave it there.</p>`;
      return;
    }
    host.innerHTML = displays.map((d, n) => `
      <div class="display" data-n="${n}">
        <div class="top">
          <input class="nm" data-k="name" value="${esc(d.name)}" placeholder="Name">
          <span class="spacer"></span>
          <a class="btn small" href="${esc(displayUrl(d))}" target="_blank" rel="noopener">Open</a>
          <button class="ghost small" data-act="copy">Copy link</button>
          <button class="ghost small" data-act="del" title="Remove this display">Remove</button>
        </div>
        <div class="grid">
          <label><span>Follows</span>
            <select data-k="board">
              <option value="">nothing — runs on its own</option>
              ${devices.map(v => `<option value="${esc(v.device_id)}"${
                v.device_id === d.board ? " selected" : ""}>${esc(v.device)}</option>`).join("")}
            </select></label>
          <label><span>Shows</span>
            <select data-k="mode">
              ${MODE_CHOICES.map(m => `<option value="${m}"${
                m === d.mode ? " selected" : ""}>${esc(pretty(m))}</option>`).join("")}
            </select></label>
          <label><span>Cycle every (s)</span>
            <input type="number" data-k="cycle_interval" min="5" max="3600" value="${d.cycle_interval}"></label>
          <label><span>Digit gap</span>
            <input type="number" data-k="digit_gap" min="0" max="2" step="0.05" value="${d.digit_gap}"></label>
        </div>
        <div class="swatches">
          <label><input type="checkbox" data-k="mirror"${d.mirror ? " checked" : ""}> Follow the wall's mode</label>
          <label><input type="color" data-k="hand_color" value="${esc(d.hand_color)}"> Hands</label>
          <label><input type="color" data-k="background" value="${esc(d.background)}"> Background</label>
          <label><input type="checkbox" data-k="show_face"${d.show_face ? " checked" : ""}> Faces</label>
          <label><input type="color" data-k="face_color" value="${esc(d.face_color)}"> Face</label>
        </div>
      </div>`).join("");

    host.querySelectorAll(".display").forEach(el => {
      const n = Number(el.dataset.n);
      el.querySelectorAll("[data-k]").forEach(inp => {
        inp.onchange = () => {
          const k = inp.dataset.k;
          displays[n][k] = inp.type === "checkbox" ? inp.checked
                         : inp.type === "number" ? Number(inp.value) : inp.value;
          saveDisplays();
        };
      });
      el.querySelector('[data-act="del"]').onclick = () => {
        if (!confirm(`Remove "${displays[n].name}"? Any tablet pointed at it will stop working.`)) return;
        displays.splice(n, 1);
        saveDisplays().then(renderDisplays);
      };
      el.querySelector('[data-act="copy"]').onclick = async (ev) => {
        // The absolute URL, including the Ingress prefix - a relative one is
        // useless on the tablet you are about to paste it into.
        const url = new URL(displayUrl(displays[n]), location.href).href;
        try { await navigator.clipboard.writeText(url); ev.target.textContent = "Copied"; }
        catch { prompt("Copy this link:", url); }
        setTimeout(() => { ev.target.textContent = "Copy link"; }, 1400);
      };
    });
  }

  async function saveDisplays() {
    try {
      const r = await api("api/displays", { displays });
      displays = r.displays;
    } catch (err) {
      setLive("err", `<b>Could not save displays.</b> ${esc(err.message)}`);
    }
  }

  $("adddisplay").onclick = async () => {
    const n = displays.length + 1;
    displays.push({
      id: "d" + Date.now().toString(36), name: "Display " + n,
      board: board ? board.device_id : "", mirror: true, mode: "cycle",
      cycle: "", cycle_interval: 120, digit_gap: 0,
      hand_color: "#ffffff", background: "#000000",
      face_color: "#1f1f23", show_face: false,
    });
    await saveDisplays();
    renderDisplays();
  };

  (async () => {
    try {
      const r = await api("api/displays");
      displays = r.displays || [];
    } catch { displays = []; }
    renderDisplays();
  })();

  // ---- the card ----------------------------------------------------------
  // Two halves: the FILE has to be in /config/www, and Lovelace has to know
  // about it. Copying the file is easy over REST; the resource registry is
  // WebSocket-only, which is what hass_ws.py exists for.
  async function cardStatus() {
    const note = $("cardnote"), btn = $("register"), state = $("resstate");
    try {
      const r = await api("api/card");
      if (!r.installed) {
        state.textContent = "";
        btn.classList.add("hidden");
        note.innerHTML = `Not installed — ${esc(r.error || "turn on install_card in the add-on options")}.`;
        return;
      }
      if (r.registered) {
        state.textContent = "installed and registered";
        btn.classList.add("hidden");
        note.innerHTML = `<code>${esc(r.url)}</code> is a registered Lovelace ` +
          `resource. Add any of these as a manual card:`;
      } else if (r.can_register) {
        state.textContent = "";
        btn.classList.remove("hidden");
        btn.disabled = false;
        btn.textContent = "Install card";
        note.innerHTML = `The bundle is at <code>${esc(r.url)}</code> but Lovelace ` +
          `does not know about it yet. <b>Install card</b> registers it — then add ` +
          `any of these as a manual card:`;
      } else {
        // Almost always `lovelace: mode: yaml`, where the registry is not the
        // source of truth. Say so instead of offering a button that cannot work.
        state.textContent = "";
        btn.classList.add("hidden");
        note.innerHTML = `The bundle is at <code>${esc(r.url)}</code>. Registering it ` +
          `automatically is not possible here (${esc(r.why || "resources unavailable")}) — ` +
          `add it under <b>Settings → Dashboards → ⋮ → Resources</b>, or in your ` +
          `<code>lovelace:</code> YAML, as a <b>JavaScript module</b>. Then:`;
      }
    } catch (err) {
      $("cardnote").textContent = "Card status unavailable: " + err.message;
    }
  }

  $("register").onclick = async () => {
    const btn = $("register");
    btn.disabled = true;
    btn.textContent = "Installing…";
    try { await api("api/card/register", {}); }
    catch (err) { setLive("err", `<b>Could not register the card.</b> ${esc(err.message)}`); }
    await cardStatus();
  };

  cardStatus();

  if (!window.CC) {
    setLive("err", "<b>Engine missing.</b> Run <code>stage.sh</code> and rebuild the add-on.");
  } else {
    refresh().then(startPolling);
  }
})();
