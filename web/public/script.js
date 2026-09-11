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

// Manual page: collapsible §-sections on mobile (accordion), a scroll-spy
// that highlights the current section in the sidebar TOC on desktop, and a
// live search box that filters sections by their text. All three are no-ops
// anywhere but manual.html, since they bail out immediately if the page has
// no .manual-section elements.
(function () {
  var deviceScreen = document.getElementById("deviceScreen");
  var sections = document.querySelectorAll(".manual-section");
  var tocLinks = document.querySelectorAll(".toc a[href^='#']");
  if (!sections.length) return;

  var mq = window.matchMedia("(max-width: 899px)");
  var targetId = location.hash ? location.hash.slice(1) : null;

  var entries = [];
  sections.forEach(function (sec) {
    var head = sec.querySelector(".sec-head");
    var heading = sec.querySelector("h2[id]");
    if (!head || !heading) return;
    var isTarget = targetId && heading.id === targetId;
    if (!isTarget) sec.classList.add("collapsed");

    function toggle() {
      if (!mq.matches) return; // accordion only applies on mobile widths
      sec.classList.toggle("collapsed");
      syncOne();
    }
    function syncOne() {
      if (mq.matches) {
        head.setAttribute("role", "button");
        head.setAttribute("tabindex", "0");
        head.setAttribute("aria-expanded", sec.classList.contains("collapsed") ? "false" : "true");
      } else {
        head.removeAttribute("role");
        head.removeAttribute("tabindex");
        head.removeAttribute("aria-expanded");
      }
    }
    head.addEventListener("click", toggle);
    head.addEventListener("keydown", function (e) {
      if (e.key === "Enter" || e.key === " ") { e.preventDefault(); toggle(); }
    });
    entries.push({ sec: sec, head: head, heading: heading, sync: syncOne });
  });

  function syncAllA11y() { entries.forEach(function (e) { e.sync(); }); }
  syncAllA11y();
  mq.addEventListener("change", syncAllA11y);

  // Any same-page hash navigation (a TOC link, or an in-text "see §4"
  // cross-reference) expands the section it points at, instead of landing
  // you on a visible heading with hidden content underneath it.
  window.addEventListener("hashchange", function () {
    var id = location.hash.slice(1);
    var match = entries.filter(function (e) { return e.heading.id === id; })[0];
    if (match) { match.sec.classList.remove("collapsed"); match.sync(); }
  });

  // ---- scroll-spy: highlight the current section in the sidebar TOC ----
  // Deliberately not IntersectionObserver's usual "band near the top"
  // technique: with a section as long as §3 Screens, that only lights up
  // the TOC entry for a brief moment as the heading passes through the
  // band, then goes dark for the rest of the time you're actually reading
  // it. Instead: the "current" section is whichever heading is the last
  // one to have scrolled up past the anchor line - it stays current for
  // the section's whole length, and only changes once the next heading
  // reaches that same line.
  var updateActive = function () {}; // reassigned below if scroll-spy is active; runFilter() calls it either way
  if (tocLinks.length && deviceScreen) {
    var linkFor = {};
    tocLinks.forEach(function (a) { linkFor[a.getAttribute("href").slice(1)] = a; });
    var ANCHOR = 90; // px from the top of #deviceScreen
    var ticking = false;

    updateActive = function () {
      ticking = false;
      var containerTop = deviceScreen.getBoundingClientRect().top;
      var currentId = null;
      entries.forEach(function (e) {
        if (e.sec.hidden) return; // skip anything the search filter hid
        var top = e.heading.getBoundingClientRect().top - containerTop;
        if (top <= ANCHOR) currentId = e.heading.id;
      });
      tocLinks.forEach(function (a) { a.classList.remove("active"); });
      if (currentId && linkFor[currentId]) linkFor[currentId].classList.add("active");
    };
    function onScroll() {
      if (ticking) return;
      ticking = true;
      requestAnimationFrame(updateActive);
    }
    deviceScreen.addEventListener("scroll", onScroll, { passive: true });
    updateActive();
  }

  // ---- live search/filter ----
  var input = document.getElementById("manualSearch");
  var clearBtn = document.getElementById("manualSearchClear");
  var wrap = document.getElementById("manualSearchWrap");
  if (input) {
    var linkForId = {};
    tocLinks.forEach(function (a) { linkForId[a.getAttribute("href").slice(1)] = a; });

    function runFilter() {
      var q = input.value.trim().toLowerCase();
      var anyMatch = false;
      entries.forEach(function (e) {
        var match = !q || e.sec.textContent.toLowerCase().indexOf(q) !== -1;
        e.sec.hidden = !match;
        var link = linkForId[e.heading.id];
        if (link) link.style.display = match ? "" : "none";
        if (match) {
          anyMatch = true;
          if (q) { e.sec.classList.remove("collapsed"); e.sync(); }
        }
      });
      wrap.classList.toggle("no-match", !!q && !anyMatch);
      updateActive();
    }
    input.addEventListener("input", runFilter);
    if (clearBtn) {
      clearBtn.addEventListener("click", function () {
        input.value = "";
        runFilter();
        input.focus();
      });
    }
  }
})();
