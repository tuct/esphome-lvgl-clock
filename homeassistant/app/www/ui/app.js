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
  function chips(host, options, current, onPick, label, after) {
    host.innerHTML = "";
    options.forEach(opt => {
      const value = typeof opt === "string" ? opt : opt.value;
      const b = document.createElement("button");
      b.className = "chip" + (opt.cls ? " " + opt.cls : "");
      b.type = "button";
      b.setAttribute("aria-pressed", String(value === current));
      b.innerHTML = typeof opt === "string" ? esc(opt) : opt.html;
      if (opt.title) b.title = opt.title;
      else if (label) b.title = `${label}: ${value}`;
      b.onclick = async () => {
        if (b.getAttribute("aria-pressed") === "true") return;
        host.querySelectorAll(".chip").forEach(c => c.classList.add("busy"));
        try { await onPick(value); }
        catch (err) { setLive("err", `<b>Failed.</b> ${esc(err.message)}`); }
        finally { host.querySelectorAll(".chip").forEach(c => c.classList.remove("busy")); }
        (after || refresh)();
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

  // The same slider the wall uses, but driven by a plain value rather than an
  // entity - a display is stored in the add-on, not on the master.
  function plainSlider(el, out, opts) {
    el.min = opts.min; el.max = opts.max; el.step = opts.step;
    if (document.activeElement !== el) el.value = opts.value;
    out.textContent = opts.fmt(el.value);
    el.oninput = () => { out.textContent = opts.fmt(el.value); };
    el.onchange = () => opts.onChange(Number(el.value));
  }

  const esc = s => String(s).replace(/[&<>"]/g, c =>
    ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));

  // Mode names are snake_case in the firmware because they are also the wire
  // format. Reading them is nicer than typing them.
  const pretty = m => m.replace(/_/g, " ");

  // ---- patterns as modes -------------------------------------------------
  // The slots that have something in them. An empty one draws nothing, so it
  // is not something to offer - and the name is the pattern's own, because "3"
  // tells you nothing.
  function namedPatterns() {
    if (!board) return [];
    return board.slots
      .filter(s => s.state)
      .map(s => ({ slot: s.slot, name: s.state.split(":")[0] || `pattern ${s.slot}` }));
  }

  const curSlot = () => {
    const p = board && board.controls.pattern_slot;
    return p ? Number(p.state) || 0 : 0;
  };

  // The name of whatever the wall is showing, in the same vocabulary the lists
  // use - so `pattern` reads back as the pattern it is actually drawing.
  function showing() {
    const c = board ? board.controls : {};
    if (!c.mode) return null;
    if (c.mode.state !== "pattern") return c.mode.state;
    const p = namedPatterns().find(x => x.slot === curSlot());
    return p ? p.name : "pattern";
  }

  // One click, whichever kind of thing was picked. A pattern is two writes -
  // the slot first, so the wall never spends a frame drawing the pattern that
  // happened to be loaded before.
  async function pickMode(v) {
    const c = board.controls;
    if (v.indexOf("pattern:") === 0) {
      const slot = v.slice(8);
      if (c.pattern_slot && c.pattern_slot.state !== slot)
        await call("select", "select_option",
                   { entity_id: c.pattern_slot.entity_id, option: slot });
      if (c.mode.state !== "pattern")
        await call("select", "select_option",
                   { entity_id: c.mode.entity_id, option: "pattern" });
      return;
    }
    await call("select", "select_option", { entity_id: c.mode.entity_id, option: v });
  }

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

    // Mode - the firmware's modes and your own patterns in ONE list. `pattern`
    // was never a thing anyone wanted to pick on its own: it always meant a
    // particular pattern, and saying which one took a second control and a
    // second click. A pattern the wall is holding is a mode, so it sits here.
    $("f-mode").classList.toggle("hidden", !c.mode);
    if (c.mode) {
      const pats = namedPatterns();
      const opts = c.mode.options
        // The bare entry survives only when there is nothing to replace it
        // with - a wall whose slots are all still empty, where it is the only
        // way in.
        .filter(o => o !== "pattern" || !pats.length)
        .map(o => ({ value: o, html: esc(pretty(o)) }))
        .concat(pats.map(p => ({
          value: "pattern:" + p.slot, cls: "pat",
          title: `pattern ${p.slot}`, html: esc(p.name),
        })));
      const slot = curSlot();
      const cur = c.mode.state === "pattern" && slot ? "pattern:" + slot : c.mode.state;
      chips($("modes"), opts, cur, pickMode, "mode");
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
    if (c.cycle_modes && !editing && !(cycleChips && cycleChips.dragging())) syncCycle(c);

    $("reload").classList.toggle("hidden", !c.reload_patterns);
    $("resetlook").classList.toggle("hidden", !c.reset_look);
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
    const hand = (c.hand_color && c.hand_color.state) || "#ffffff";
    const back = (c.bg_color && c.bg_color.state) || "#000000";

    if (!preview) {
      // Built once. Rebuilding it to change mode or colour would restart the
      // animation from the card's starting pose - a jump, in a preview of a
      // thing whose whole point is that it never jumps.
      const cfg = { type: "custom:clockclock24-card", mode: "time", digit_gap: 0,
                    hand_color: hand, background: back };
      preview = document.createElement("clockclock24-card");
      try { preview.setConfig(cfg); }
      catch (err) { preview = null; return; }
      $("preview").classList.remove("empty");
      $("preview").appendChild(preview);
      previewKey = "";
    }
    const key = [mode, pattern || "", hand, back].join("|");
    if (key === previewKey) return;
    previewKey = key;

    preview.setColors(hand, back);
    // `pattern` with an empty slot has nothing to draw - show the clock.
    if (mode === "pattern" && !pattern) preview.setMode("time");
    else if (mode === "pattern") preview.setMode("pattern", pattern);
    else preview.setMode(mode);
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

  // ---- cycle list --------------------------------------------------------
  // A list you can drag, not a comma-separated string you have to retype. The
  // string is still the truth - the master parses it and republishes what it
  // accepted - but nobody should have to edit it by hand to move `wind` one
  // place left.
  let cycle = [];
  // Which names in the list are patterns rather than firmware modes. Only used
  // to mark them; the master neither knows nor cares about the difference.
  let patternNames = [];

  function syncCycle(c) {
    cycle = (c.cycle_modes.state || "").split(",").map(s => s.trim()).filter(Boolean);
    $("cycleinput").value = cycle.join(",");
    // What you can add: every mode the wall knows and every pattern that has
    // something in it, in ONE list rather than a modes list with a patterns
    // annexe. The master takes either by name, so the distinction was ours,
    // not its.
    //
    // The bare `pattern` entry stays, because in a cycle list it means
    // something no named entry does: take the NEXT one in the store each time
    // round. Labelled, since that is not guessable from the word.
    const modes = c.mode ? c.mode.options : [];
    patternNames = namedPatterns().map(p => p.name).filter(n => !modes.includes(n));
    const keep = $("cycleadd").value;
    $("cycleadd").innerHTML =
      modes.map(m => `<option value="${esc(m)}">${esc(pretty(m))}${
        m === "pattern" ? " — next in the store" : ""}</option>`).join("") +
      patternNames.map(n => `<option value="${esc(n)}">${esc(n)}</option>`).join("");
    if ([...modes, ...patternNames].includes(keep)) $("cycleadd").value = keep;
    drawCycle();
  }

  // A draggable list of chips, shared by the wall's cycle list and each
  // display's. `get`/`set` are the only things that differ - one writes to an
  // entity, the other to a saved display.
  //
  // Pointer events, not HTML5 drag-and-drop: this add-on gets opened on the
  // tablet it drives, and dragstart never fires from a finger.
  function chipList(host, opts) {
    const state = { drag: null };
    const api = { draw, dragging: () => state.drag !== null };
    function draw() {
      const list = opts.get();
      host.innerHTML = list.length
        ? list.map((m, i) =>
            `<span class="tag${m === api.now ? " now" : ""}${i === state.drag ? " dragging" : ""}"` +
            ` data-i="${i}"><span class="i">${i + 1}</span>` +
            (opts.isPattern && opts.isPattern(m) ? `<span class="dot"></span>` : "") +
            `${esc(pretty(m))}` +
            `<button class="x" title="Remove" aria-label="Remove ${esc(m)}">&times;</button></span>`).join("")
        : `<span class="tag empty">${esc(opts.empty)}</span>`;
      host.querySelectorAll(".tag[data-i]").forEach(el => {
        const i = Number(el.dataset.i);
        el.querySelector(".x").onclick = (e) => {
          e.stopPropagation();
          const l = opts.get(); l.splice(i, 1); opts.set(l); draw();
        };
        el.onpointerdown = (e) => {
          if (e.target.classList.contains("x")) return;
          state.drag = i; el.setPointerCapture(e.pointerId); draw();
        };
        el.onpointermove = (e) => {
          if (state.drag === null) return;
          const over = document.elementFromPoint(e.clientX, e.clientY);
          const tag = over && over.closest ? over.closest(".tag[data-i]") : null;
          if (!tag) return;
          const j = Number(tag.dataset.i);
          if (j === state.drag) return;
          const l = opts.get();
          l.splice(j, 0, l.splice(state.drag, 1)[0]);
          state.drag = j; opts.set(l, true); draw();
        };
        const end = () => {
          if (state.drag === null) return;
          state.drag = null; draw(); opts.set(opts.get());
        };
        el.onpointerup = end;
        el.onpointercancel = end;
      });
    }
    return api;
  }

  let cycleChips = null;
  function drawCycle() {
    if (!cycleChips) {
      cycleChips = chipList($("cycletags"), {
        get: () => cycle,
        set: (l, quiet) => { cycle = l; if (!quiet) commitCycle(); },
        empty: "empty — the wall stays on whatever Mode says",
        isPattern: m => patternNames.includes(m),
      });
    }
    // `showing()`, not the raw mode: an entry that names a pattern would never
    // light up against a mode state of `pattern`.
    cycleChips.now = showing();
    cycleChips.draw();
  }

  async function commitCycle() {
    const c = board.controls.cycle_modes;
    try { await call("text", "set_value", { entity_id: c.entity_id, value: cycle.join(",") }); }
    catch (err) { setLive("err", `<b>Failed.</b> ${esc(err.message)}`); }
    // The master drops names it does not know and republishes what it kept, so
    // what comes back is the answer - not what was sent.
    setTimeout(refresh, 400);
  }

  $("cycleaddbtn").onclick = () => {
    const v = $("cycleadd").value;
    if (!v) return;
    cycle.push(v);          // repeats are meaningful, so no de-duplication
    drawCycle();
    commitCycle();
  };

  $("cycletext").onclick = () => {
    editing = !editing;
    $("cycleeditrow").classList.toggle("hidden", editing === false);
    if (editing) $("cycleinput").focus();
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
  // Both of these throw work away, so both ask first.
  const pressButton = (id, role, question) => {
    $(id).onclick = async () => {
      const c = board.controls[role];
      if (!c || !confirm(question)) return;
      try { await call("button", "press", { entity_id: c.entity_id }); }
      catch (err) { setLive("err", `<b>Failed.</b> ${esc(err.message)}`); }
      setTimeout(refresh, 900);
    };
  };
  pressButton("reload", "reload_patterns",
    "Discard patterns written from Home Assistant and go back to the " +
    "patterns/ folder as compiled in?");
  pressButton("resetlook", "reset_look",
    "Discard the saved movement, sweep length, speed, colours, cycle list and " +
    "interval, and go back to what the firmware was compiled with?");

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

  // A display runs the card, not the wall: no `temp` (there is no sensor
  // behind it) and no `pattern` (the slots live on the master). Everything
  // else is the same set the wall offers, presented the same way.
  const DISPLAY_MODES = ["cycle", "time", "rotate_left", "flying_birds", "wave",
                         "spiral", "wind", "rotating_maze", "zipper",
                         "mirror_wave", "love"];
  const INTERVALS = [30, 60, 120, 180, 300, 600, 900, 1800, 3600];
  const MOVEMENTS = ["opposite", "clockwise", "counter", "long"];

  const secLabel = s => (s < 60 ? s + " s" : (s / 60) + " min");

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

        <div class="field">
          <div class="flabel">Follows<span class="fnote">mirror a real wall, or run on its own</span></div>
          <div class="inline">
            <div class="selwrap"><select data-k="board">
              <option value="">nothing — runs on its own</option>
              ${devices.map(v => `<option value="${esc(v.device_id)}"${
                v.device_id === d.board ? " selected" : ""}>${esc(v.device)}</option>`).join("")}
            </select></div>
            <label class="chk"><input type="checkbox" data-k="mirror"${
              d.mirror ? " checked" : ""}> follow its mode and colours</label>
          </div>
        </div>

        <div class="field">
          <div class="flabel">Mode<span class="fnote">what this screen shows</span></div>
          <div class="chips" data-chips="mode" data-n="${n}"></div>
        </div>

        <div class="split">
          <div class="field">
            <div class="flabel">Cycle every</div>
            <div class="chips tight" data-chips="interval" data-n="${n}"></div>
          </div>
          <div class="field">
            <div class="flabel">Window<span class="fnote">seconds of choreography</span></div>
            <div class="sliderrow"><input type="range" data-sl="window" data-n="${n}"><output></output></div>
          </div>
        </div>

        <div class="field">
          <div class="flabel">Movement<span class="fnote">how the hands travel to a new digit</span></div>
          <div class="chips tight" data-chips="movement" data-n="${n}"></div>
        </div>

        <div class="split">
          <div class="field">
            <div class="flabel">Transition</div>
            <div class="sliderrow"><input type="range" data-sl="transition" data-n="${n}"><output></output></div>
          </div>
          <div class="field">
            <div class="flabel">Mode speed</div>
            <div class="sliderrow"><input type="range" data-sl="mode_speed" data-n="${n}"><output></output></div>
          </div>
        </div>

        <div class="field">
          <div class="flabel">Cycle list<span class="fnote">drag to reorder — empty uses the card's own six</span></div>
          <div class="taglist" data-cycle="${n}"></div>
          <div class="addrow">
            <div class="selwrap"><select data-cycleadd="${n}">${DISPLAY_MODES
              .filter(m => m !== "cycle" && m !== "time")
              .map(m => `<option value="${m}">${esc(pretty(m))}</option>`).join("")}</select></div>
            <button class="ghost small" data-cycleaddbtn="${n}">Add</button>
          </div>
        </div>

        <div class="field">
          <div class="flabel">Look<span class="fnote">this screen only</span></div>
          <div class="swatchrow">
            <label><input type="color" data-k="hand_color" value="${esc(d.hand_color)}"> Hands</label>
            <label><input type="color" data-k="background" value="${esc(d.background)}"> Background</label>
            <label class="chk"><input type="checkbox" data-k="show_face"${d.show_face ? " checked" : ""}> Faces</label>
            <label><input type="color" data-k="face_color" value="${esc(d.face_color)}"> Face</label>
            <label class="chk"><input type="checkbox" data-k="return_to_time"${
              d.return_to_time ? " checked" : ""}> back to the time between windows</label>
          </div>
          <div class="sliderrow" style="margin-top:12px">
            <span class="slabel">Digit gap</span>
            <input type="range" data-sl="digit_gap" data-n="${n}"><output></output>
          </div>
        </div>
      </div>`).join("");

    host.querySelectorAll(".display").forEach(el => {
      const n = Number(el.dataset.n);
      const d = displays[n];
      const save = () => saveDisplays();

      el.querySelectorAll("[data-k]").forEach(inp => {
        inp.onchange = () => {
          d[inp.dataset.k] = inp.type === "checkbox" ? inp.checked : inp.value;
          save();
        };
      });

      const pick = (what, options, current, key, map) =>
        chips(el.querySelector(`[data-chips="${what}"]`), options, current,
              v => { d[key] = map ? map(v) : v; save(); }, what,
              () => renderDisplays());

      pick("mode", DISPLAY_MODES.map(m => ({ value: m, html: esc(pretty(m)) })), d.mode, "mode");
      pick("interval", INTERVALS.map(s => ({ value: String(s), html: esc(secLabel(s)) })),
           String(d.cycle_interval), "cycle_interval", v => Number(v));
      pick("movement", MOVEMENTS.map(m => ({ value: m, html: esc(m) })), d.movement, "movement");

      const sl = (what, opts) => plainSlider(
        el.querySelector(`[data-sl="${what}"]`),
        el.querySelector(`[data-sl="${what}"]`).nextElementSibling, opts);
      sl("window", { min: 5, max: 180, step: 1, value: d.window,
                     fmt: v => v + " s", onChange: v => { d.window = v; save(); } });
      sl("transition", { min: 0.2, max: 20, step: 0.1, value: d.transition / 1000,
                         fmt: v => Number(v).toFixed(1) + " s",
                         onChange: v => { d.transition = Math.round(v * 1000); save(); } });
      sl("mode_speed", { min: 0.1, max: 5, step: 0.1, value: d.mode_speed,
                         fmt: v => "×" + Number(v).toFixed(1),
                         onChange: v => { d.mode_speed = v; save(); } });
      sl("digit_gap", { min: 0, max: 1, step: 0.05, value: d.digit_gap,
                        fmt: v => Number(v) === 0 ? "none (like the wall)" : Number(v).toFixed(2),
                        onChange: v => { d.digit_gap = v; save(); } });

      el.querySelector('[data-act="del"]').onclick = () => {
        if (!confirm(`Remove "${d.name}"? Any tablet pointed at it will stop working.`)) return;
        displays.splice(n, 1);
        saveDisplays().then(renderDisplays);
      };
      el.querySelector('[data-act="copy"]').onclick = async (ev) => {
        // The absolute URL, including the Ingress prefix - a relative one is
        // useless on the tablet you are about to paste it into.
        const url = new URL(displayUrl(d), location.href).href;
        try { await navigator.clipboard.writeText(url); ev.target.textContent = "Copied"; }
        catch { prompt("Copy this link:", url); }
        setTimeout(() => { ev.target.textContent = "Copy link"; }, 1400);
      };

      const chipsHost = el.querySelector(`[data-cycle="${n}"]`);
      const cyc = chipList(chipsHost, {
        get: () => (d.cycle || "").split(",").map(s => s.trim()).filter(Boolean),
        set: (l, quiet) => { d.cycle = l.join(","); if (!quiet) save(); },
        empty: "empty — the card cycles its own six",
      });
      cyc.draw();
      el.querySelector(`[data-cycleaddbtn="${n}"]`).onclick = () => {
        const l = (d.cycle || "").split(",").map(s => s.trim()).filter(Boolean);
        l.push(el.querySelector(`[data-cycleadd="${n}"]`).value);
        d.cycle = l.join(",");
        cyc.draw();
        save();
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
      mode_speed: 1, transition: 5000,
      movement: "opposite", window: 35, return_to_time: true,
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
