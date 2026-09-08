// The device's own move is "turn, turn, turn, turn, click" to cycle
// screens — so that's the knock on this site's door too. Nobody but the
// owner needs the monitor, so it isn't in the nav; type the sequence
// below anywhere on the page and it'll take you there.
(function () {
  var seq = ["ArrowUp", "ArrowUp", "ArrowDown", "ArrowDown", "Enter"];
  var pos = 0;
  document.addEventListener("keydown", function (e) {
    if (e.key === seq[pos]) {
      pos++;
      if (pos === seq.length) {
        pos = 0;
        window.location.href = "p-f838d1.html";
      }
    } else {
      pos = e.key === seq[0] ? 1 : 0;
    }
  });
})();

// The site now lives inside the device's own bezel (#deviceScreen is the
// "glass"), with a real encoder and button fixed beside it. This wires the
// controls to what's actually happening on the page instead of leaving
// them as decoration: the knob spins a step on every scroll tick, in the
// direction you're scrolling, and the button visibly clicks down on any
// tap or click — including a beat before following a link, so the press
// is actually visible before the page navigates.
(function () {
  var screen = document.getElementById("deviceScreen");
  var mark = document.getElementById("encoderMark");
  var btn = document.getElementById("deviceButton");
  if (!screen) return;

  var reduceMotion = window.matchMedia && window.matchMedia("(prefers-reduced-motion: reduce)").matches;

  // A raw "scroll" event doesn't mean the same amount of scrolling on
  // every device: a mouse wheel fires a handful of chunky-delta events,
  // but touch/momentum scrolling on a phone fires many more, smaller
  // ones for the same distance — ticking once per event made the knob
  // spin far too fast on mobile. Instead we accumulate actual scrolled
  // pixels and only advance the knob once that adds up to one tick, so
  // the rotation tracks real scroll distance the same way on any device.
  var TICK_PX = 32;
  var angle = 18;
  var acc = 0;
  var lastTop = screen.scrollTop;
  screen.addEventListener(
    "scroll",
    function () {
      if (reduceMotion || !mark) return;
      var top = screen.scrollTop;
      var delta = top - lastTop;
      lastTop = top;
      if (delta === 0) return;
      acc += delta;
      var moved = false;
      while (acc >= TICK_PX) {
        angle += 14;
        acc -= TICK_PX;
        moved = true;
      }
      while (acc <= -TICK_PX) {
        angle -= 14;
        acc += TICK_PX;
        moved = true;
      }
      if (moved) mark.style.transform = "translateX(-50%) rotate(" + angle + "deg)";
    },
    { passive: true }
  );

  var pressTimer = null;
  function press() {
    if (!btn) return;
    btn.classList.add("pressed");
    clearTimeout(pressTimer);
    pressTimer = setTimeout(function () {
      btn.classList.remove("pressed");
    }, 140);
  }

  screen.addEventListener("click", function (e) {
    press();

    var link = e.target.closest && e.target.closest("a[href]");
    if (!link) return;
    if (e.defaultPrevented || e.metaKey || e.ctrlKey || e.shiftKey || e.altKey) return;
    if (link.target === "_blank") return;

    var href = link.getAttribute("href");
    if (!href || href.charAt(0) === "#" || href.indexOf("http") === 0 || href.indexOf("mailto:") === 0) return;

    // Give the click a beat to actually show before the page unloads.
    e.preventDefault();
    setTimeout(function () {
      window.location.href = href;
    }, 110);
  });
})();


// Manual §3's Notion template has a "Copy" button next to it
// (data-copy-target points at the <pre> holding the plain-text template) -
// copies its exact text so it pastes into Notion as real blocks rather
// than however the browser would render the styled page around it. Tries
// the modern Clipboard API first, falls back to a hidden-textarea
// execCommand("copy") (with the selection quirks Safari/iOS need to
// actually honor it), and if even that's unavailable, selects the
// template's own text so the button never claims "Copied!" when nothing
// actually landed on the clipboard - a plain Ctrl/Cmd+C (or the mobile
// selection menu's Copy) still works on whatever's now highlighted.
(function () {
  var buttons = document.querySelectorAll("[data-copy-target]");
  if (!buttons.length) return;

  function selectText(el) {
    var range = document.createRange();
    range.selectNodeContents(el);
    var sel = window.getSelection();
    sel.removeAllRanges();
    sel.addRange(range);
  }

  function execCommandCopy(text) {
    var ta = document.createElement("textarea");
    ta.value = text;
    ta.setAttribute("readonly", "");
    ta.style.position = "absolute";
    ta.style.left = "-9999px";
    ta.style.top = "0";
    document.body.appendChild(ta);
    ta.focus();
    ta.select();
    ta.setSelectionRange(0, text.length); // iOS Safari ignores a plain select()
    var ok = false;
    try { ok = document.execCommand("copy"); } catch (e) { ok = false; }
    document.body.removeChild(ta);
    return ok;
  }

  buttons.forEach(function (btn) {
    var original = btn.textContent;

    function setLabel(label, ms) {
      btn.textContent = label;
      btn.disabled = true;
      setTimeout(function () {
        btn.textContent = original;
        btn.disabled = false;
      }, ms);
    }

    btn.addEventListener("click", function () {
      var target = document.getElementById(btn.getAttribute("data-copy-target"));
      if (!target) return;
      var text = target.innerText || target.textContent;

      function onFailure() {
        selectText(target);
        setLabel("Selected \u2013 press Ctrl/Cmd+C", 2200);
      }

      if (navigator.clipboard && navigator.clipboard.writeText) {
        navigator.clipboard.writeText(text).then(
          function () { setLabel("Copied!", 1400); },
          function () { if (execCommandCopy(text)) setLabel("Copied!", 1400); else onFailure(); }
        );
      } else if (document.queryCommandSupported && document.queryCommandSupported("copy")) {
        if (execCommandCopy(text)) setLabel("Copied!", 1400); else onFailure();
      } else {
        onFailure();
      }
    });
  });
})();
