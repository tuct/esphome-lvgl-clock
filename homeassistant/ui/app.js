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
  let card = null;
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

  const esc = s => String(s).replace(/[&<>"]/g, c =>
    ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));

  // Mode names are snake_case in the firmware because they are also the wire
  // format. Reading them is nicer than typing them.
  const pretty = m => m.replace(/_/g, " ");

  // ---- render ------------------------------------------------------------
  function render() {
    const c = board ? board.controls : {};
    $("badge").classList.toggle("hidden", !board || !board.is_master);
    $("control").classList.toggle("hidden", !board);
    $("editorpanel").classList.toggle("hidden", !board);

    if (!board) return;
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

    $("tablet").href = "display.html?mirror=1&board=" + encodeURIComponent(board.device_id);
    $("reload").classList.toggle("hidden", !c.reload);
    $("temp").textContent = board.temperature && board.temperature.state
      ? `${board.temperature.name}: ${board.temperature.state}` : "";

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
      const keep = board && board.device_id;
      sel.innerHTML = devices.length
        ? devices.map(d => `<option value="${esc(d.device_id)}">${esc(d.device)}${
            d.is_master ? "" : " (no marker)"}</option>`).join("")
        : `<option value="">nothing found</option>`;
      if (devices.some(d => d.device_id === keep)) sel.value = keep;
      board = devices.find(d => d.device_id === sel.value) || devices[0] || null;

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
    board = devices.find(d => d.device_id === $("board").value) || null;
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

  // ---- cards note --------------------------------------------------------
  (async () => {
    try {
      const r = await api("api/card");
      $("cardnote").innerHTML = r.installed
        ? `Installed at <code>/local/${r.file}</code>. Register it once under ` +
          `<b>Settings → Dashboards → ⋮ → Resources → Add</b>, type ` +
          `<b>JavaScript module</b>, then add any of these as a manual card:`
        : `Not installed — ${esc(r.error || "turn on install_card in the add-on options")}.`;
    } catch { $("cardnote").textContent = "Card status unavailable."; }
  })();

  if (!window.CC) {
    setLive("err", "<b>Engine missing.</b> Run <code>stage.sh</code> and rebuild the add-on.");
  } else {
    refresh().then(startPolling);
  }
})();
