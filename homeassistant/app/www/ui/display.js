// The tablet view: the wall, full bleed, and nothing else.
//
// It is the LOVELACE CLOCK CARD - the same file a dashboard would load, with
// the same engine as the firmware. This page only sizes it and, if asked,
// keeps it in step with the real wall.
//
// Query string, all optional:
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
  const mirror = q.get("mirror") === "1";

  const cfg = {
    type: "custom:clockclock24-card",
    fullscreen: true,
    digit_gap: q.has("gap") ? Number(q.get("gap")) : 0,
    mode: q.get("mode") || "cycle",
    cycle_interval: Number(q.get("interval") || 120),
  };
  if (q.get("cycle")) cfg.cycle = q.get("cycle").split(",").map(s => s.trim()).filter(Boolean);

  let card = null;
  let shown = "";               // what the card is currently configured to show

  function mount(extra) {
    const next = Object.assign({}, cfg, extra || {});
    const key = JSON.stringify(next);
    if (key === shown) return;
    shown = key;
    if (card) card.remove();
    card = document.createElement("clockclock24-card");
    card.setConfig(next);
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
      const b = q.get("board")
        ? boards.find(d => d.device_id === q.get("board"))
        : boards.find(d => d.is_master) || boards[0];
      if (!b) { note.textContent = "No wall found — running on its own."; return; }

      const mode = b.controls.mode && b.controls.mode.state;
      const slotNo = b.controls.pattern_slot && Number(b.controls.pattern_slot.state);
      const slot = slotNo ? b.slots[slotNo - 1] : null;
      note.textContent = `${b.device} · ${mode || "?"}`;

      if (!mode) return;
      if (mode === "pattern" && slot && slot.state)
        mount({ mode: "pattern", pattern: slot.state });
      else if (mode !== "pattern")
        mount({ mode, pattern: null });
    } catch (err) {
      note.textContent = "Home Assistant unreachable — running on its own.";
    }
  }

  mount();
  if (mirror) {
    follow();
    setInterval(() => { if (!document.hidden) follow(); }, 3000);
  } else {
    note.textContent = cfg.mode === "cycle" ? "Cycling on its own" : cfg.mode;
  }

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
