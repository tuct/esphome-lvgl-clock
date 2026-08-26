// The tablet view: the wall, full bleed, and nothing else.
//
// It is the LOVELACE CLOCK CARD - the same file a dashboard would load, with
// the same engine as the firmware. This page only sizes it and, if asked,
// keeps it in step with the real wall.
//
// Query string, all optional:
//   ?d=<id>          a display saved in the add-on: its look and its board
//   ?mirror=1        follow the real wall's mode and pattern slot
//   ?board=<id>      which board to follow (default: the first master found)
//   ?mode=wave       a fixed mode instead of the rotation
//   ?cycle=a,b,c     the rotation to run
//   ?interval=120    seconds between windows
//   ?gap=0           digit gap, in clock widths

(() => {
  const q = new URLSearchParams(location.search);
  const host = document.getElementById("host");
  const note = document.getElementById("note");

  // Base config from the query string. A saved display (?d=<id>) overrides it,
  // so a hand-written link still works and a configured one does not need one.
  const cfg = {
    type: "custom:clockclock24-card",
    fullscreen: true,
    digit_gap: q.has("gap") ? Number(q.get("gap")) : 0,
    mode: q.get("mode") || "cycle",
    cycle_interval: Number(q.get("interval") || 120),
  };
  if (q.get("cycle")) cfg.cycle = q.get("cycle").split(",").map(s => s.trim()).filter(Boolean);

  let mirror = q.get("mirror") === "1";
  // Which look the saved display pinned - those win over the wall's.
  const custom = {};
  let boardId = q.get("board") || null;
  let title = "";

  async function loadSaved(id) {
    const r = await fetch("api/displays").then(x => x.json());
    const d = (r.displays || []).find(x => x.id === id);
    if (!d) throw new Error("that display has been removed");
    title = d.name;
    document.title = d.name + " — Clock Clock 24";
    mirror = !!d.mirror;
    boardId = d.board || null;
    cfg.mode = d.mode || "cycle";
    cfg.cycle_interval = d.cycle_interval;
    cfg.digit_gap = d.digit_gap;
    cfg.hand_color = d.hand_color;
    cfg.background = d.background;
    custom.hand_color = true;
    custom.background = true;
    cfg.face_color = d.face_color;
    cfg.show_face = d.show_face;
    if (d.cycle) cfg.cycle = d.cycle.split(",").map(s => s.trim()).filter(Boolean);
    if (d.mode_speed) cfg.mode_speed = d.mode_speed;
    if (d.transition) cfg.transition = d.transition;
    if (d.movement) cfg.movement = d.movement;
    if (d.window) cfg.window = d.window;
    cfg.return_to_time = d.return_to_time !== false;
    // The page behind the card has to match, or a non-black display shows a
    // black border wherever the 8:3 wall does not reach the screen edge.
    document.documentElement.style.background = d.background;
    document.body.style.background = d.background;
    // #rrggbb + alpha. Only for a 6-digit hex - #abc + "cc" is not a colour,
    // and the default black fade is a fine fallback.
    if (/^#[0-9a-fA-F]{6}$/.test(d.background))
      document.documentElement.style.setProperty("--chrome-fade", d.background + "cc");
  }

  let card = null;
  let shown = "";               // what the card is currently configured to show

  function mount() {
    if (card) return;
    card = document.createElement("clockclock24-card");
    card.setConfig(cfg);
    host.appendChild(card);
  }

  // ---- mirroring ---------------------------------------------------------
  // Rebuilding the card restarts its animation, so this only touches it when
  // the wall has ACTUALLY changed mode. A tablet that stutters every three
  // seconds is worse than one that is a second behind.
  async function follow() {
    try {
      const r = await fetch("api/discover").then(x => x.json());
      const boards = r.devices || [];
      const b = boardId
        ? boards.find(d => d.device_id === boardId)
        : boards.find(d => d.is_master) || boards[0];
      if (!b) { note.textContent = "No wall found — running on its own."; return; }

      const mode = b.controls.mode && b.controls.mode.state;
      // A mirroring display follows the wall's colours too, unless the saved
      // display set its own.
      const hand = custom.hand_color ? null : (b.controls.hand_color || {}).state;
      const back = custom.background ? null : (b.controls.bg_color || {}).state;
      const slotNo = b.controls.pattern_slot && Number(b.controls.pattern_slot.state);
      const slot = slotNo ? b.slots[slotNo - 1] : null;
      note.textContent = (title ? title + " · " : "") + `${b.device} · ${mode || "?"}`;

      if (!mode) return;
      // setMode, NOT a rebuild. Recreating the card threw away where every
      // hand was, so the wall snapped to the new card's starting pose and only
      // then swept to the time - which is exactly what "it cannot jump" is
      // there to prevent. The card blends into a mode and settles out of one
      // by itself, given the chance.
      if (mode === "pattern" && slot && slot.state) card.setMode("pattern", slot.state);
      else if (mode !== "pattern") card.setMode(mode);
      if (hand || back) card.setColors(hand, back);
    } catch (err) {
      note.textContent = "Home Assistant unreachable — running on its own.";
    }
  }

  (async () => {
    if (q.get("d")) {
      try { await loadSaved(q.get("d")); }
      catch (err) { note.textContent = "Display not found — " + err.message; }
    }
    mount();
    if (mirror) {
      follow();
      setInterval(() => { if (!document.hidden) follow(); }, 3000);
    } else {
      note.textContent = title || (cfg.mode === "cycle" ? "Cycling on its own" : cfg.mode);
    }
  })();

  // ---- chrome ------------------------------------------------------------
  let sleep = null;
  const wake = () => {
    document.body.classList.add("awake");
    clearTimeout(sleep);
    sleep = setTimeout(() => document.body.classList.remove("awake"), 3000);
  };
  ["pointerdown", "pointermove", "keydown"].forEach(e =>
    document.addEventListener(e, wake, { passive: true }));
  wake();

  document.getElementById("full").onclick = () => {
    // Inside Ingress this is an iframe; requestFullscreen needs the frame to
    // allow it, which is not ours to set. If it is refused, the page is still
    // full bleed - that is the part that matters.
    const el = document.documentElement;
    if (document.fullscreenElement) document.exitFullscreen();
    else if (el.requestFullscreen) el.requestFullscreen().catch(() => {
      note.textContent = "Open this page in its own browser tab for true full screen.";
    });
  };
})();
