// "Seven screens. One knob." as a scroll run: the 3D device stays pinned in
// the middle of #deviceScreen, boots like the real one, then the feature
// cards slide right-to-left behind it and its panel switches to whichever
// card has most recently gone behind it.
//
// Model geometry is lifted from enclosure/flugvel_case.scad (via
// flugvel-3d.html). Screen drawing is ported from the captive portal's
// preview (src/network/PortalPage.cpp body()/pv()), which itself mirrors the
// firmware's draw code, so the coordinates match the panel.
//
// Progressive: without WebGL, or with reduced motion, nothing here runs and
// the cards stay the plain bento grid.
import * as THREE from "https://cdn.jsdelivr.net/npm/three@0.184.0/build/three.module.js";

const section = document.getElementById("screensRun");
const scroller = document.getElementById("deviceScreen");
const reduce = window.matchMedia("(prefers-reduced-motion: reduce)").matches;
const gl = document.createElement("canvas").getContext("webgl2");
if (section && scroller && !reduce && gl) start();

function start() {
  const pin = section.querySelector(".run-pin");
  const track = section.querySelector(".run-track");
  const cards = [...track.children];
  const glCanvas = section.querySelector(".run-device");
  const masthead = document.querySelector(".masthead-bar");
  section.classList.add("run-live");

  // ---------------------------------------------------------------- panel
  const W = 320, H = 240, SCALE = 3; // drawn at 3x so text survives the texture
  const panel = document.createElement("canvas");
  panel.width = W * SCALE; panel.height = H * SCALE;
  const ctx = panel.getContext("2d");
  ctx.scale(SCALE, SCALE);
  const t = { bg: "#c8d0b8", fg: "#2a2e22", dim: "#4a5038", rule: "#8a9078", ac: "#c05a1e", ok: "#5a7a2a" };

  // TFT_eSPI font 1: 6x8 px per glyph at size 1. z is pixel height (8/16/24/32).
  const R = (x, y, w, h, c) => { ctx.fillStyle = c; ctx.fillRect(x, y, w, h); };
  const T = (x, y, s, c, z = 8, a = "left") => {
    ctx.font = `600 ${z * 1.05}px "IBM Plex Mono", monospace`;
    if ("letterSpacing" in ctx) ctx.letterSpacing = `${z * 0.75 - z * 0.63}px`;
    ctx.textAlign = a; ctx.textBaseline = "top"; ctx.fillStyle = c;
    ctx.fillText(s, x, y - z * 0.08);
  };

  const QUOTE = ["The propeller is just a big fan to keep the pilot cool. Stop it and watch him sweat.", "Every flight instructor"];

  const SCREENS = {
    plane: ["Plane tracker", () => {
      R(0, 26, W, 1, t.fg); R(0, 193, W, 1, t.fg); R(0, 26, 1, 168, t.fg); R(319, 26, 1, 168, t.fg);
      T(18, 36, "KLM1071", t.ac, 16); R(14, 60, 292, 1, t.rule);
      T(18, 74, "ALTITUDE", t.dim); T(18, 88, "10500 m", t.fg, 16);
      T(160, 74, "SPEED", t.dim); T(160, 88, "842 km/h", t.fg, 16);
      T(18, 118, "HEADING", t.dim); T(18, 132, "265 W", t.fg, 16);
      T(160, 118, "FROM", t.dim); T(160, 132, "Netherlands", t.fg, 16);
      // PlaneTrackerScreen::drawQuote - strip under the box, 10px lines, 3 lines + author
      const words = QUOTE[0].split(" ");
      let line = "\"", y = 198;
      for (const w of words) {
        if ((line + " " + w).length * 6 > 316) { T(2, y, line, t.dim); y += 10; line = w; }
        else line += (line === "\"" ? "" : " ") + w;
      }
      T(2, y, line + "\"", t.dim);
      T(316, y + 10, "- " + QUOTE[1], t.dim, 8, "right");
    }, false],
    weather: ["Weather", () => {
      T(10, 24, "12 C", t.fg, 24); T(100, 26, "Partly cloudy", t.dim); T(100, 44, "Amsterdam, NL", t.dim);
      T(10, 58, "Day", t.fg); T(65, 58, "Condition", t.fg); T(240, 58, "Hi", t.fg); T(280, 58, "Lo", t.fg);
      R(10, 69, 300, 1, t.rule);
      [["Mon", "Clear", 14, 6], ["Tue", "Cloudy", 13, 7], ["Wed", "Rain", 11, 8], ["Thu", "Rain", 10, 6],
       ["Fri", "Clear", 12, 4], ["Sat", "Clear", 15, 5]].forEach((d, i) => {
        const y = 74 + i * 19, s = i === 0;
        if (s) R(6, y - 3, 306, 17, t.fg);
        T(10, y, d[0], s ? t.bg : t.dim); T(65, y, d[1], s ? t.bg : t.dim);
        T(240, y, d[2] + "C", s ? t.bg : t.fg); T(280, y, d[3] + "C", s ? t.bg : t.dim);
      });
    }],
    calendar: ["Calendar", () => {
      [["Today", "09:30", "Standup"], ["Today", "14:00", "Design review"], ["Tomorrow", "11:00", "Dentist"],
       ["Thu 11", "19:30", "Flight KL1071"], ["Sat 13", "08:00", "Parkrun"], ["Mon 15", "all day", "Deadline"]]
        .forEach((e, i) => {
          const y = 26 + i * 30, s = i === 0;
          R(6, y, 308, 28, s ? t.fg : t.bg); if (s) R(6, y, 4, 28, t.ac);
          T(16, y + 3, e[0], s ? t.bg : t.ac); T(16, y + 15, e[1], s ? t.bg : t.dim);
          T(84, y + 6, e[2], s ? t.bg : t.fg, 16);
        });
    }],
    notes: ["Notes", () => { // NotesScreen::drawProgress/drawList, ROW_H 24
      T(6, 24, "TO-DO", t.dim); T(314, 24, "2/5 done", t.dim, 8, "right");
      R(6, 36, 308, 3, t.rule); R(6, 36, 123, 3, t.ok);
      [["h", "Saturday"], [0, "Order filament"], [1, "Flash v1.0.13"], [1, "Print new knob"],
       [0, "Solder encoder"], [0, "Email Sam back"]].forEach((n, i) => {
        const y = 44 + i * 24, s = i === 1, fg = s ? t.bg : t.fg;
        R(6, y, 308, 22, s ? t.fg : t.bg); if (s) R(6, y, 4, 22, t.ac);
        if (n[0] === "h") { T(14, y + 3, "v " + n[1], t.ac, 16); R(6, y + 21, 308, 1, t.rule); return; }
        ctx.strokeStyle = n[0] ? t.dim : fg; ctx.lineWidth = 1; ctx.strokeRect(13.5, y + 5.5, 9, 9);
        if (n[0]) { ctx.beginPath(); ctx.moveTo(15, y + 11); ctx.lineTo(17, y + 13); ctx.lineTo(22, y + 7); ctx.stroke(); }
        T(32, y + 3, n[1], n[0] ? t.dim : fg, 16);
      });
    }],
    focus: ["Focus timer", () => {
      T(14, 34, "SESSION", t.dim); R(14, 46, 292, 1, t.rule);
      R(10, 56, 300, 34, t.fg); R(10, 56, 4, 34, t.ac);
      T(26, 66, "FOCUS", t.bg, 16); T(250, 66, "25 min", t.bg, 16, "center");
      T(26, 106, "BREAK", t.dim, 16); T(250, 106, "5 min", t.fg, 16, "center");
      T(14, 138, "starts now", t.dim);
    }],
    games: ["Games", () => {
      [["Flappy Plane", "BEST 27"], ["Paddle Catch", "BEST 14"], ["Reaction Timer", "SOON"], ["Simon Says", "SOON"]]
        .forEach((g, i) => {
          const y = 44 + i * 36, s = i === 0;
          R(6, y, 308, 36, s ? t.fg : t.bg); if (s) R(6, y, 4, 36, t.ac);
          T(18, y + 10, g[0], s ? t.bg : t.fg, 16); T(306, y + 14, g[1], s ? t.bg : t.dim, 8, "right");
        });
    }],
    settings: ["Settings", () => { // SettingsScreen::drawMainMenu
      for (let x = 10; x < 310; x += 7) R(x, 104, 3, 1, t.rule);
      [["Device info", ""], ["Theme", "Mono"], ["Configure on phone", ""], ["Software update", "v1.0.13"],
       ["Restart device", ""], ["Factory reset", ""]].forEach((o, i) => {
        const y = 22 + i * 20 + (i >= 4 ? 4 : 0), s = i === 0;
        R(6, y, 308, 20, s ? t.fg : t.bg); if (s) R(6, y, 4, 20, t.ac);
        T(18, y + 4, o[0], s ? t.bg : i >= 4 ? "#b03020" : t.fg, 16);
        if (o[1]) T(308, y + 9, o[1] + " >", s ? t.bg : t.dim, 8, "right");
      });
    }],
  };
  const ORDER = cards.map((c) => c.dataset.screen);

  function drawScreen(key) {
    const [name, body, legend = true] = SCREENS[key];
    const pos = ORDER.indexOf(key), n = ORDER.length;
    R(0, 0, W, H, t.bg); R(0, 19, W, 1, t.rule);
    T(4, 6, `[ ${name.toUpperCase()} ]`, t.fg);
    const sp = (n - 1) * 9 + 4, ls = Math.round((W + sp) / 2) - 2;
    for (let i = n - 1; i >= 0; i--) { // Header pager dots
      const cx = ls - (n - 1 - i) * 9;
      ctx.beginPath(); ctx.arc(cx, 9, 2, 0, 7);
      if (i === pos) { ctx.fillStyle = t.fg; ctx.fill(); } else { ctx.strokeStyle = t.rule; ctx.stroke(); }
    }
    T(W - 4, 6, "14:05", t.dim, 8, "right");
    body();
    if (legend) { R(0, 210, W, 30, t.bg); R(0, 210, W, 1, t.rule); T(6, 216, "^v SELECT", t.dim); T(6, 228, "o ENTER", t.fg); }
  }

  // BootScreen::show, squeezed into a 0..1 progress value
  function drawBoot(p) {
    R(0, 0, W, H, t.bg);
    const name = "[ FLUGVEL ]";
    if (p < 0.5) { // phase 1: typed name with blinking cursor
      const k = p / 0.5 * name.length, typed = Math.floor(k);
      const startX = (320 - name.length * 24) / 2;
      for (let i = 0; i < typed; i++) T(startX + i * 24 + 10, 100, name[i], t.fg, 32, "center");
      if ((k % 1) < 0.5) R(startX + typed * 24, 100, 6, 32, t.fg);
      return;
    }
    const q = (p - 0.5) / 0.5; // phase 2: welcome + segmented bar
    T(160, 58, "[ Welcome ]", t.fg, 24, "center");
    ctx.strokeStyle = t.fg; ctx.lineWidth = 1; ctx.strokeRect(60.5, 150.5, 199, 14);
    const segs = Math.floor(Math.min(1, q * 1.15) * 16);
    for (let i = 0; i < segs; i++) R(62 + i * 12, 152, 10, 11, t.fg);
    T(60, 130, q < 0.55 ? "Connecting to Wi-Fi..." : q < 0.85 ? "Loading data..." : "Ready!", t.dim);
    T(160, 187, "Powered by KML", t.dim, 16, "center");
  }

  // ---------------------------------------------------------------- model
  const renderer = new THREE.WebGLRenderer({ canvas: glCanvas, alpha: true, antialias: true });
  renderer.setPixelRatio(Math.min(2, window.devicePixelRatio));
  const scene = new THREE.Scene();
  const camera = new THREE.PerspectiveCamera(26, 1, 0.01, 10);
  scene.add(new THREE.HemisphereLight(0xffffff, 0x6d7560, 1.6));
  const key = new THREE.DirectionalLight(0xffffff, 2.4);
  key.position.set(0.4, 0.8, 1); scene.add(key);
  const rim = new THREE.DirectionalLight(0xfff1e0, 1.1);
  rim.position.set(-1, 0.3, -0.6); scene.add(rim);

  const BOARD_L = 97, BOARD_W = 50, WALL = 2, CORNER_R = 4;
  const CASE_L = BOARD_L + 2 * WALL, CASE_W = BOARD_W + 2 * WALL;
  const BODY_D = WALL + 11 + 1.8 + 9 + 6, FRONT_Z = BODY_D / 2;
  const DISP_L = 50, DISP_W = 40, DISP_X = 4, DISP_Y = (BOARD_W - DISP_W) / 2;
  const ENC_D = 7.2, ENC_X = 74, ENC_Y = 36, BTN_D = 6.2, BTN_X = 74, BTN_Y = 15;
  const mx = (x) => x - CASE_L / 2, my = (y) => y - CASE_W / 2;

  const rounded = (P, cx, cy, w, h, r) => {
    const p = new P(), x = cx - w / 2, y = cy - h / 2;
    p.moveTo(x + r, y);
    p.lineTo(x + w - r, y); p.quadraticCurveTo(x + w, y, x + w, y + r);
    p.lineTo(x + w, y + h - r); p.quadraticCurveTo(x + w, y + h, x + w - r, y + h);
    p.lineTo(x + r, y + h); p.quadraticCurveTo(x, y + h, x, y + h - r);
    p.lineTo(x, y + r); p.quadraticCurveTo(x, y, x + r, y);
    return p;
  };
  const circle = (cx, cy, r) => { const p = new THREE.Path(); p.absarc(cx, cy, r, 0, Math.PI * 2, true); return p; };
  const std = (color, roughness, metalness = 0) => new THREE.MeshStandardMaterial({ color, roughness, metalness });
  const M = { // "Olive field" finish
    shell: std(0x3c4235, 0.7, 0.08), face: std(0x32362b, 0.55, 0.08), knob: std(0x3a3a3a, 0.4, 0.34),
    glass: std(0x14170f, 0.2, 0.14), brass: std(0xb08c5e, 0.4, 0.35), mark: std(0xe9e9e9, 0.45),
  };
  const add = (parent, geo, mat, x = 0, y = 0, z = 0, rotX = 0) => {
    const m = new THREE.Mesh(geo, mat); m.position.set(x, y, z); m.rotation.x = rotX; parent.add(m); return m;
  };
  const cyl = (rt, rb, h, seg = 48) => new THREE.CylinderGeometry(rt, rb, h, seg);
  const HALF_PI = Math.PI / 2;

  const model = new THREE.Group();
  const face = rounded(THREE.Shape, 0, 0, CASE_L, CASE_W, CORNER_R);
  face.holes.push(rounded(THREE.Path, mx(WALL + DISP_X + DISP_L / 2), my(WALL + DISP_Y + DISP_W / 2), DISP_L, DISP_W, 1.5));
  face.holes.push(circle(mx(WALL + ENC_X), my(WALL + ENC_Y), ENC_D / 2));
  face.holes.push(circle(mx(WALL + BTN_X), my(WALL + BTN_Y), BTN_D / 2));
  const bevel = { bevelEnabled: true, bevelSegments: 3, curveSegments: 32 };
  add(model, new THREE.ExtrudeGeometry(face, { ...bevel, depth: WALL - 0.7, bevelThickness: 0.45, bevelSize: 0.7 }),
    M.face, 0, 0, FRONT_Z - WALL + 0.05);
  add(model, new THREE.ExtrudeGeometry(rounded(THREE.Shape, 0, 0, CASE_L - 0.2, CASE_W - 0.2, CORNER_R),
    { ...bevel, depth: BODY_D - WALL - 0.9, bevelThickness: 0.4, bevelSize: 0.5 }), M.shell, 0, 0, -BODY_D / 2);

  const dx = mx(WALL + DISP_X + DISP_L / 2), dy = my(WALL + DISP_Y + DISP_W / 2);
  add(model, new THREE.BoxGeometry(DISP_L, DISP_W, 1.6), M.glass, dx, dy, FRONT_Z - WALL - 1.1);
  const tex = new THREE.CanvasTexture(panel);
  tex.colorSpace = THREE.SRGBColorSpace;
  tex.anisotropy = renderer.capabilities.getMaxAnisotropy();
  const sw = DISP_L - 1.2, sh = sw * 240 / 320;
  // Unlit, like a backlit panel: shows the theme colours exactly under any light.
  add(model, new THREE.PlaneGeometry(sw, sh), new THREE.MeshBasicMaterial({ map: tex }), dx, dy, FRONT_Z - WALL - 0.04);

  const knob = new THREE.Group();
  add(knob, cyl(3, 3, 6, 32), M.knob, 0, 0, 1.4, HALF_PI);
  add(knob, cyl(ENC_D / 2 + 0.6, ENC_D / 2 + 0.6, 1, 32), M.knob, 0, 0, 0.1, HALF_PI);
  add(knob, cyl(10, 9.4, 8, 64), M.knob, 0, 0, 6, HALF_PI);
  add(knob, cyl(9.4, 8.2, 0.9, 64), M.knob, 0, 0, 9.65, HALF_PI);
  add(knob, new THREE.BoxGeometry(1.4, 5, 0.6), M.mark, 0, 5.6, 10.2);
  knob.position.set(mx(WALL + ENC_X), my(WALL + ENC_Y), FRONT_Z - WALL + 0.2);
  model.add(knob);
  const bx = mx(WALL + BTN_X), by = my(WALL + BTN_Y);
  add(model, cyl(5.4, 5, 4), M.brass, bx, by, FRONT_Z - WALL + 2, HALF_PI);
  add(model, cyl(2.6, 2.6, 0.4, 32), M.knob, bx, by, FRONT_Z - WALL + 4.1, HALF_PI);

  const root = new THREE.Group();
  root.add(model);
  root.scale.setScalar(0.001);
  scene.add(root);

  // ---------------------------------------------------------------- scroll
  let viewH = 0, dist = 0, trackW = 0, pinW = 0, last = "", knobTarget = 0, knobAngle = 0, sway = 0, needs = true;

  function layout() {
    const top = masthead ? masthead.offsetHeight : 0;
    viewH = scroller.clientHeight - top;
    pinW = pin.clientWidth;
    pin.style.top = top + "px";
    pin.style.height = viewH + "px";
    // cards one device-width apart, so the next one goes in as the last comes out
    // as big as fits: the panel is ~half the case, and 320x240 text blurs once it shrinks below 1:1
    const devW = Math.min(pinW * (pinW < 620 ? 0.98 : 0.64), viewH * 0.56 / 0.6, 900);
    glCanvas.style.width = devW + "px";
    glCanvas.style.height = devW * 0.6 + "px";
    track.style.setProperty("--gap", Math.max(24, devW - cards[0].offsetWidth * 0.6) + "px");
    trackW = track.scrollWidth;
    dist = pinW + trackW;
    section.style.height = viewH + dist * 1.1 + viewH * 0.9 + "px"; // boot + travel + a little dwell
    renderer.setSize(devW, devW * 0.6, false);
    camera.aspect = 1 / 0.6;
    // fit the case width with some room for the sway
    camera.position.set(0, 0, (0.101 * 0.6) / Math.tan(THREE.MathUtils.degToRad(13)) / camera.aspect);
    camera.updateProjectionMatrix();
    needs = true;
  }

  function frame() {
    const scrolled = scroller.scrollTop - section.offsetTop + (masthead ? masthead.offsetHeight : 0);
    const bootLen = viewH * 0.9;
    const bootP = Math.min(1, Math.max(0, scrolled / bootLen));
    const travel = Math.max(0, scrolled - bootLen) / 1.1;
    const x = pinW - Math.min(travel, dist);
    track.style.transform = `translate3d(${x}px, -50%, 0)`;

    // the last card whose leading edge has crossed the device's centre line
    const centre = pinW / 2;
    let active = -1;
    cards.forEach((c, i) => { if (x + c.offsetLeft <= centre) active = i; });
    const now = active < 0 ? "boot" + Math.round(bootP * 60) : ORDER[active];
    if (now !== last) {
      if (active < 0) drawBoot(bootP); else drawScreen(now);
      knobTarget = -Math.PI / 5 * Math.max(0, active); // one detent per screen
      cards.forEach((c, i) => c.classList.toggle("is-behind", i === active));
      tex.needsUpdate = true;
      last = now;
      needs = true;
    }

    const swayTarget = (bootP - 0.5) * 0.08 + (Math.min(travel, dist) / dist - 0.5) * -0.22; // small, so the panel stays square-on
    sway += (swayTarget - sway) * 0.12;
    knobAngle += (knobTarget - knobAngle) * 0.18;
    if (needs || Math.abs(swayTarget - sway) > 1e-4 || Math.abs(knobTarget - knobAngle) > 1e-4) {
      root.rotation.set(0.05 + sway * 0.2, sway, 0);
      knob.rotation.z = knobAngle;
      renderer.render(scene, camera);
      needs = false;
    }
    requestAnimationFrame(frame);
  }

  window.addEventListener("resize", layout);
  document.fonts.ready.then(() => { last = ""; }); // redraw once Plex Mono is in
  layout();
  requestAnimationFrame(frame);
}
