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

  // The knob turned scrolling into a visual - now it works the other way
  // too: grab it (mouse or touch, via Pointer Events so both paths share
  // one code path) and turn it to actually scroll the screen. That drives
  // #deviceScreen's own scrollTop, which is exactly what the scroll
  // listener above is already watching, so the mark keeps rotating in
  // sync for free - no separate rotation logic to keep in step.
  //
  // This tracks the pointer's ANGLE around the knob's center rather than
  // its raw vertical position. A straight vertical drag can only scroll as
  // far as your hand physically travels before it runs off the window -
  // tracking rotation instead means you can keep circling the knob
  // indefinitely (several full turns without ever running out of room),
  // same as actually spinning a dial.
  var knob = mark && mark.parentElement;
  if (knob && window.PointerEvent) {
    var dragId = null;
    var lastAngle = 0;
    var DEG_TO_PX = 2.2; // scroll pixels per degree turned

    // Angle is unstable within a few px of dead center (tiny mouse jitter
    // there swings wildly), so grabs/moves that close are ignored rather
    // than fed into the scroll math.
    function angleFromCenter(x, y) {
      var rect = knob.getBoundingClientRect();
      var dx = x - (rect.left + rect.width / 2);
      var dy = y - (rect.top + rect.height / 2);
      if (Math.hypot(dx, dy) < 10) return null;
      return Math.atan2(dy, dx) * (180 / Math.PI);
    }

    knob.addEventListener("pointerdown", function (e) {
      if (e.pointerType === "mouse" && e.button !== 0) return;
      var a = angleFromCenter(e.clientX, e.clientY);
      if (a === null) return;
      dragId = e.pointerId;
      lastAngle = a;
      knob.classList.add("dragging");
      try {
        knob.setPointerCapture(dragId);
      } catch (err) {
        /* ignore */
      }
      e.preventDefault();
    });

    knob.addEventListener("pointermove", function (e) {
      if (dragId === null || e.pointerId !== dragId) return;
      var a = angleFromCenter(e.clientX, e.clientY);
      if (a === null) return; // too close to center this frame - hold last angle
      var delta = a - lastAngle;
      // Normalize across the +-180deg wrap so crossing it doesn't register
      // as a near-360deg jump.
      if (delta > 180) delta -= 360;
      if (delta < -180) delta += 360;
      lastAngle = a;
      screen.scrollTop += delta * DEG_TO_PX;
    });

    function stopDrag(e) {
      if (dragId === null || (e && e.pointerId !== dragId)) return;
      try {
        knob.releasePointerCapture(dragId);
      } catch (err) {
        /* ignore */
      }
      dragId = null;
      knob.classList.remove("dragging");
    }
    knob.addEventListener("pointerup", stopDrag);
    knob.addEventListener("pointercancel", stopDrag);
  }
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

    // A plain lowercased substring match means "wifi" doesn't find "Wi-Fi",
    // "on device" doesn't find "on-device", and so on - anything the page
    // spells with a hyphen or a line break the manual's own words don't
    // match how someone would actually type them. Stripping hyphens/dashes
    // and whitespace from both sides before comparing fixes that without
    // losing real substring matching (multi-word queries like "factory
    // reset" still work, just insensitive to exactly how they're spaced).
    function normalize(s) {
      return s.toLowerCase().replace(/[-‐-―\s]+/g, "");
    }
    // Plain word-splitting for the typo-tolerant fallback below - lowercase,
    // fuse away hyphens/dashes first (same as normalize(), so "Wi-Fi" is one
    // word "wifi" instead of splitting into throwaway fragments "wi" and
    // "fi"), then break on anything else that isn't a letter/digit. Single
    // characters are dropped - too short to usefully match against.
    function wordsOf(s) {
      var out = [];
      var fused = s.replace(/[-‐-―]+/g, "");
      var parts = fused.toLowerCase().split(/[^a-z0-9]+/);
      for (var i = 0; i < parts.length; i++) {
        if (parts[i].length > 1) out.push(parts[i]);
      }
      return out;
    }
    // Damerau-Levenshtein edit distance (insert/delete/substitute, plus an
    // adjacent transposition counting as a single edit rather than two) -
    // the transposition case matters here because swapped letters ("notoin"
    // for "notion") are one of the most common typing slips, and without it
    // those would need double the typo budget of every other kind of typo.
    function editDistance(a, b) {
      if (a === b) return 0;
      var al = a.length, bl = b.length;
      var d = [];
      for (var i = 0; i <= al; i++) d[i] = [i];
      for (var j = 0; j <= bl; j++) d[0][j] = j;
      for (i = 1; i <= al; i++) {
        for (j = 1; j <= bl; j++) {
          var cost = a.charCodeAt(i - 1) === b.charCodeAt(j - 1) ? 0 : 1;
          d[i][j] = Math.min(d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + cost);
          if (i > 1 && j > 1 && a.charCodeAt(i - 1) === b.charCodeAt(j - 2) && a.charCodeAt(i - 2) === b.charCodeAt(j - 1)) {
            d[i][j] = Math.min(d[i][j], d[i - 2][j - 2] + 1);
          }
        }
      }
      return d[al][bl];
    }
    // How many typos to forgive scales with word length - kept tight (a
    // single edit) through most real words, since a 2-edit budget on a
    // 6-letter query starts coincidentally matching unrelated words (e.g.
    // "notoin" was matching "nothing") rather than actual typos.
    function typoBudget(len) {
      if (len <= 7) return 1;
      if (len <= 12) return 2;
      return 3;
    }
    // Does this query word plausibly mean one of this section's words -
    // either one contains the other (catches partial/squished typing, like
    // "flappyplane" while aiming for "Flappy Plane"), or they're within typo
    // distance of each other. The containment check only counts once both
    // words are at least 4 letters - without that floor, a query like
    // "notoin" trivially "contains" tiny common words such as "in" that
    // show up in nearly every section, and the search stops narrowing
    // anything down.
    function fuzzyWordMatch(queryWord, sectionWords) {
      var budget = typoBudget(queryWord.length);
      for (var i = 0; i < sectionWords.length; i++) {
        var w = sectionWords[i];
        if (Math.abs(w.length - queryWord.length) > budget + 2) continue; // cheap pre-filter
        if (w.length >= 4 && queryWord.length >= 4 && (w.indexOf(queryWord) !== -1 || queryWord.indexOf(w) !== -1)) return true;
        if (editDistance(queryWord, w) <= budget) return true;
      }
      return false;
    }

    entries.forEach(function (e) {
      e.searchText = normalize(e.sec.textContent);
      e.words = wordsOf(e.sec.textContent);
    });

    function runFilter() {
      var raw = input.value.trim();
      var q = normalize(raw);
      var queryWords = wordsOf(raw);
      var anyMatch = false;
      entries.forEach(function (e) {
        var match;
        if (!raw) {
          match = true;
        } else if (e.searchText.indexOf(q) !== -1) {
          match = true; // exact (hyphen/space-insensitive) match - the common case
        } else {
          // Typo-tolerant fallback: every word you typed has to be close to
          // some word in this section, even if none of it lines up exactly.
          // A 1-2 letter query word requires an exact word match rather than
          // fuzzy matching (too short for "close to" to mean anything) or a
          // substring check against the run-together searchText blob (a
          // fragment like "in" or "to" turns up inside plenty of unrelated
          // words once spaces are stripped, matching almost everything).
          match = queryWords.length > 0 && queryWords.every(function (qw) {
            return qw.length <= 2 ? e.words.indexOf(qw) !== -1 : fuzzyWordMatch(qw, e.words);
          });
        }
        e.sec.hidden = !match;
        var link = linkForId[e.heading.id];
        if (link) link.style.display = match ? "" : "none";
        if (match) {
          anyMatch = true;
          if (raw) { e.sec.classList.remove("collapsed"); e.sync(); }
        }
      });
      wrap.classList.toggle("no-match", !!raw && !anyMatch);
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

// Landing page: "Get updates" email signup (POST /api/subscribe). Requires
// JS - there's no non-JS fallback, same as the copy button and the manual
// search above, since a plain form submit would POST url-encoded data and
// the endpoint expects JSON.
(function () {
  var form = document.getElementById("signupForm");
  if (!form) return;
  var input = document.getElementById("signupEmail");
  var honeypot = document.getElementById("signupCompany");
  var status = document.getElementById("signupStatus");
  var button = form.querySelector("button[type=submit]");
  var EMAIL_RE = /^[^\s@]+@[^\s@]+\.[^\s@]+$/;

  function setStatus(text, kind) {
    status.textContent = text;
    status.className = "signup-status" + (kind ? " " + kind : "");
  }

  form.addEventListener("submit", function (e) {
    e.preventDefault();
    var email = input.value.trim();

    if (!email || !EMAIL_RE.test(email)) {
      setStatus("Enter a valid email address.", "error");
      input.focus();
      return;
    }

    var original = button.textContent;
    button.disabled = true;
    button.textContent = "Sending…";
    setStatus("", "");

    fetch("/api/subscribe", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ email: email, company: honeypot ? honeypot.value : "" }),
    })
      .then(function (res) {
        return res
          .json()
          .catch(function () { return {}; })
          .then(function (data) { return { ok: res.ok, data: data }; });
      })
      .then(function (result) {
        if (result.ok) {
          if (result.data && result.data.alreadySubscribed) {
            setStatus("You're already on the list — appreciate the enthusiasm.", "success");
          } else {
            setStatus("You're on the list.", "success");
          }
          input.value = "";
        } else {
          setStatus((result.data && result.data.error) || "Something went wrong – try again in a bit.", "error");
        }
      })
      .catch(function () {
        setStatus("Couldn't reach the server – check your connection and try again.", "error");
      })
      .then(function () {
        button.disabled = false;
        button.textContent = original;
      });
  });
})();
