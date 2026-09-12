import { readUnsubscribeToken } from "./_lib/unsubscribe-token.js";

// GET  /unsubscribe?t=<token>  - confirmation page with a button
// POST /unsubscribe            - actually removes the address
//
// Why the click is a POST and not just the link itself: corporate mail
// scanners and link-safety services (Outlook Safe Links and friends) fetch
// every URL in an incoming email to check it. If the GET did the removal,
// those subscribers would be silently unsubscribed by their own employer's
// security software, without ever seeing the email. A GET that only renders
// a page is safe to prefetch, and RFC 8058 specifies POST for exactly this
// reason.
//
// The same POST handler serves the one-click button inside Gmail/Apple Mail,
// which those clients render from the List-Unsubscribe headers on the email
// (see subscribe.js) and submit directly. That is not a browser form, so it
// carries no token of its own beyond the one in the URL and must not be
// gated on anything session-like.

const PAGE_CSS = `
  :root { color-scheme: light; }
  * { box-sizing: border-box; }
  body {
    margin: 0; min-height: 100vh; display: flex; align-items: center; justify-content: center;
    padding: 24px; background: #c7cfb6;
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Arial, sans-serif;
    color: #23271d;
  }
  .card {
    width: 100%; max-width: 460px; background: #faf9f1; border-radius: 8px;
    padding: 32px; text-align: center;
  }
  .mark {
    font-family: "Courier New", Courier, monospace; font-size: 13px; letter-spacing: 4px;
    font-weight: 700; color: #23271d;
  }
  .eyebrow {
    display: block; margin-top: 22px; font-family: "Courier New", Courier, monospace;
    font-size: 11px; letter-spacing: 2px; text-transform: uppercase; color: #c05a1e; font-weight: 700;
  }
  h1 { margin: 10px 0 0 0; font-family: Georgia, "Times New Roman", serif; font-weight: normal; font-size: 24px; line-height: 1.3; }
  p { margin: 18px 0 0 0; font-size: 15px; line-height: 1.6; color: #5c6249; }
  .addr { font-family: "Courier New", Courier, monospace; font-size: 14px; color: #23271d; word-break: break-all; }
  button {
    margin-top: 24px; border: 0; cursor: pointer; background: #c05a1e; color: #faf9f1;
    font-family: inherit; font-size: 14px; font-weight: 700; padding: 12px 28px; border-radius: 5px;
  }
  button:hover { background: #a94d18; }
  .muted { font-size: 12.5px; color: #8a9078; margin-top: 26px; }
  a { color: #c05a1e; }
`;

function page(title, bodyHtml, status = 200) {
  return new Response(
    `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>${title} - FlugVel</title>
<style>${PAGE_CSS}</style>
</head>
<body>
  <div class="card">
    <div class="mark">[&nbsp;FLUGVEL&nbsp;]</div>
    ${bodyHtml}
  </div>
</body>
</html>`,
    {
      status,
      headers: {
        "Content-Type": "text/html; charset=utf-8",
        // Nothing here should sit in a cache: the page states whether an
        // address is still subscribed, which changes the moment the button
        // is pressed.
        "Cache-Control": "no-store",
        "Referrer-Policy": "no-referrer",
        "X-Robots-Tag": "noindex, nofollow",
      },
    }
  );
}

function escapeHtml(s) {
  return String(s).replace(/[&<>"']/g, (c) => (
    { "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]
  ));
}

const badLinkPage = () =>
  page(
    "Link not valid",
    `<span class="eyebrow">Unsubscribe</span>
     <h1>This link isn't valid.</h1>
     <p>It may have been altered on its way here, or only part of it was copied.</p>
     <p class="muted">Reply to any email from FlugVel and you'll be removed by hand.</p>`,
    400
  );

const donePage = (email) =>
  page(
    "Unsubscribed",
    `<span class="eyebrow">Unsubscribed</span>
     <h1>You're off the list.</h1>
     <p class="addr">${escapeHtml(email)}</p>
     <p>That address has been removed. You won't get any more email from FlugVel.</p>
     <p class="muted">Changed your mind? You can <a href="/#signup">sign up again</a> any time.</p>`
  );

export async function onRequestGet(context) {
  const { request, env } = context;
  const token = new URL(request.url).searchParams.get("t") || "";

  const email = await readUnsubscribeToken(token, env.JWT_SECRET);
  if (!email) return badLinkPage();

  // Deliberately does NOT check whether the address is still in the database
  // before offering the button. Saying "that address isn't subscribed" would
  // turn this page into a way to test whether any given person is on the
  // list, since anyone can mint a link for an address they already know.
  return page(
    "Unsubscribe",
    `<span class="eyebrow">Unsubscribe</span>
     <h1>Remove this address?</h1>
     <p class="addr">${escapeHtml(email)}</p>
     <p>You'll stop getting FlugVel update emails. This takes effect immediately.</p>
     <form method="POST" action="/unsubscribe">
       <input type="hidden" name="t" value="${escapeHtml(token)}">
       <button type="submit">Unsubscribe me</button>
     </form>
     <p class="muted">Didn't mean to click this? Just close the page - nothing changes until you press the button.</p>`
  );
}

export async function onRequestPost(context) {
  const { request, env } = context;

  // The token can arrive two ways: from this page's own form (a normal form
  // POST), or in the query string when a mail client submits the one-click
  // List-Unsubscribe URL itself - those send their own body
  // ("List-Unsubscribe=One-Click") and expect the URL to carry everything.
  let token = new URL(request.url).searchParams.get("t") || "";
  if (!token) {
    try {
      const form = await request.formData();
      token = String(form.get("t") || "");
    } catch {
      /* no body, or not form-encoded - falls through to the invalid-link page */
    }
  }

  const email = await readUnsubscribeToken(token, env.JWT_SECRET);
  if (!email) return badLinkPage();

  if (!env.DB) {
    return page(
      "Something went wrong",
      `<span class="eyebrow">Unsubscribe</span>
       <h1>That didn't work.</h1>
       <p>Reply to any email from FlugVel and you'll be removed by hand.</p>`,
      500
    );
  }

  // Deleted outright rather than flagged: the request is to be removed, and
  // keeping a row about someone who asked to be forgotten in order to
  // remember that they asked is the wrong trade at this size of list. A
  // later signup from the same address is simply a new subscriber.
  //
  // Already gone is success, not an error - a second click, or a scanner
  // following the same link twice, should land on the same friendly page.
  await env.DB.prepare(`DELETE FROM subscribers WHERE email = ?`).bind(email).run();

  return donePage(email);
}
