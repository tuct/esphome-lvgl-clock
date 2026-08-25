// Headless regression check: NO HAND MAY EVER JUMP.
//
// That is the invariant this whole engine exists to protect — it is an
// analogue clock, so a hand can only ever sweep. Every mode is driven through
// its full lifecycle (enter from the time, run, settle back) and the largest
// single-frame movement of any of the 48 hands is measured.
//
//   /System/Library/Frameworks/JavaScriptCore.framework/Versions/A/Helpers/jsc check.js
//
// A real jump shows up as 90-180 deg in one frame. Anything under ~15 deg at
// 30 fps is a normal sweep.

var window = {};
load("engine.js");
load("wall.js");
load("pattern.js");
var C = window.CC;

var FPS = 30, DT = 1 / FPS;
var LIMIT_DEG = 20;          // per frame, at 30 fps
var fails = 0;

function run(mode, transitionMs, modeSpeed, expectJump) {
  var w = new C.Wall();
  w.transitionMs = transitionMs;
  w.modeSpeed = modeSpeed;
  var now = 0;
  function steps(n, label, track) {
    var worst = 0, at = -1, prev = Float64Array.from(w.cur);
    for (var i = 0; i < n; i++) {
      now += DT * 1000;
      w.frame(now, DT);
      if (track) for (var h = 0; h < C.NUM_HANDS; h++) {
        var d = Math.abs(C.shortestDelta(prev[h], w.cur[h]));
        if (d > worst) { worst = d; at = h; }
      }
      prev = Float64Array.from(w.cur);
    }
    return { worst: worst, at: at, label: label };
  }
  steps(30, "idle", false);                       // settle on the time first
  w.setMode(mode, now);
  var a = steps(FPS * 12, "enter+run", true);     // blend in, then run
  w.setMode("time", now);
  var b = steps(FPS * 12, "settle", true);        // and back to the time

  var worst = Math.max(a.worst, b.worst);
  var ok = expectJump ? (worst > LIMIT_DEG) : (worst <= LIMIT_DEG);
  if (!ok) fails++;
  print((ok ? "  ok  " : " FAIL ") + pad(mode, 12) +
        " enter+run " + pad(a.worst.toFixed(2), 6) +
        "  settle " + pad(b.worst.toFixed(2), 6) + " deg/frame");
  return worst;
}
function pad(s, n) { s = String(s); while (s.length < n) s += " "; return s; }

// A blank pattern is 24 still clocks, which proves nothing - give it motion so
// the check actually exercises it.
(function seedPattern() {
  var P = C.pattern;
  for (var i = 0; i < C.NUM_CLOCKS; i++) {
    var s = P.get(i);
    s.a0 = 0; s.a1 = 180; s.dirA = 1; s.dirB = -1;
    s.spdA = (i === 0) ? {mode:"fixed", v:1} : {mode:"rel", from:"left", d:-0.05};
    s.spdB = (i === 0) ? {mode:"fixed", v:1} : {mode:"rel", from:"left", d:-0.05};
  }
})();

var modes = [];
for (var k in C.MODES) if (k !== "time") modes.push(k);

print("no-jump check @ " + FPS + " fps, limit " + LIMIT_DEG + " deg/frame\n");
// The last row is a CONTROL, not a config anyone should ship: transition 0
// means "teleport" by design (`if (transition_ms_ == 0) k = 0` in the C++), so
// it must jump. If it ever stops jumping, the detector has broken, not the
// engine.
[[5000, 1.0, false], [2000, 1.0, false], [5000, 3.0, false], [0, 1.0, true]].forEach(function (cfg) {
  print("transition " + (cfg[0] / 1000) + " s, mode_speed " + cfg[1].toFixed(1) +
        (cfg[2] ? "   (control: must jump)" : ""));
  modes.forEach(function (m) { run(m, cfg[0], cfg[1], cfg[2]); });
  print("");
});

print(fails === 0 ? "PASS — nothing jumped" : "FAIL — " + fails + " case(s) jumped");
