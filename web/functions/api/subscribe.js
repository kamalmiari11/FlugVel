import { json } from "../_lib/auth.js";
import { WELCOME_EMAIL_SUBJECT, WELCOME_EMAIL_TEXT, WELCOME_EMAIL_HTML } from "../_lib/welcome-email.js";

// A deliberately loose but real email check - not trying to fully validate
// RFC 5322, just catching "clearly not an email" (empty, no @, no dot after
// the @) without rejecting anything a real address might legitimately look
// like (plus-addressing, subdomains, etc).
const EMAIL_RE = /^[^\s@]+@[^\s@]+\.[^\s@]+$/;

// Signups from the same IP beyond this many within WINDOW_MINUTES get a 429
// instead of hitting D1/Resend. Generous for the legitimate case (nobody
// signs up 6 times in 10 minutes) while capping how fast a bot - or someone
// abusing this as a free way to spam an inbox via the welcome email - can
// burn through the Resend sending quota.
const RATE_LIMIT_MAX = 5;
const RATE_LIMIT_WINDOW_MINUTES = 10;

// POST /api/subscribe  { email: string, company?: string }
// Public endpoint (no login needed) for the "Get updates" form on the
// landing page. Saves the address to D1 and, if RESEND_API_KEY is
// configured, asks Resend to send a short welcome email.
//
// The D1 write and the Resend call are independent on purpose: a signup
// always gets recorded even if Resend is down or not configured yet
// (matches the "collect now, wire up sending later" path this could have
// started from), and a slow/failed Resend call never turns a successful
// signup into an error shown to the visitor.
//
// RESEND_FROM must be set to an address on a domain verified in Resend
// (e.g. "FlugVel <updates@flugvel.com>") - without it, Resend falls back to
// its shared sandbox sender, which only allows sending to the account's own
// email address. See DEPLOY.md.
//
// There's no unsubscribe link in the welcome email yet - Resend's own
// suppression-list handling needs a verified sending domain, which is now
// in place, but the list is small enough that "reply to this email and
// I'll remove you" is still a fine stand-in. Revisit before this list gets
// much bigger.
export async function onRequestPost(context) {
  const { request, env } = context;

  let body;
  try {
    body = await request.json();
  } catch {
    return json({ error: "Invalid request body" }, { status: 400 });
  }

  // Honeypot: "company" is a hidden field real visitors never see or fill
  // (see index.html's signup form and its CSS) but simple bots that
  // auto-fill every input often do. A non-empty value means it wasn't a
  // person - pretend success without touching D1 or Resend, so the bot has
  // no signal to adapt on.
  if (body && typeof body.company === "string" && body.company.trim() !== "") {
    return json({ ok: true, alreadySubscribed: false });
  }

  const email = ((body && body.email) || "").trim().toLowerCase();
  if (!email || email.length > 320 || !EMAIL_RE.test(email)) {
    return json({ error: "Enter a valid email address" }, { status: 400 });
  }

  if (!env.DB) {
    return json({ error: "Server not configured (missing DB binding)" }, { status: 500 });
  }

  // Rate limit by IP, same CF-Connecting-IP pattern as /api/login. Kept
  // separate from login_attempts - this is a soft per-window throttle with
  // no lockout, not a security gate, so it doesn't need locked_until.
  const ip = request.headers.get("CF-Connecting-IP") || "unknown";
  try {
    const attempts = await env.DB.prepare(
      `SELECT count FROM subscribe_attempts
       WHERE ip = ? AND window_start > datetime('now', '-${RATE_LIMIT_WINDOW_MINUTES} minutes')`
    ).bind(ip).first();
    if (attempts && attempts.count >= RATE_LIMIT_MAX) {
      return json({ error: "Too many signups from this connection. Try again later." }, { status: 429 });
    }

    await env.DB.prepare(
      `INSERT INTO subscribe_attempts (ip, count, window_start)
       VALUES (?, 1, datetime('now'))
       ON CONFLICT(ip) DO UPDATE SET
         count = CASE
           WHEN window_start <= datetime('now', '-${RATE_LIMIT_WINDOW_MINUTES} minutes') THEN 1
           ELSE count + 1
         END,
         window_start = CASE
           WHEN window_start <= datetime('now', '-${RATE_LIMIT_WINDOW_MINUTES} minutes') THEN datetime('now')
           ELSE window_start
         END`
    ).bind(ip).run();
  } catch {
    // Best-effort, same reasoning as everything else in this file - a rate
    // limiter glitching should never block a real signup.
  }

  // INSERT OR IGNORE: signing up twice - or an address already on the
  // list - is a no-op rather than a UNIQUE-constraint error. D1 still
  // reports whether a row actually got inserted via meta.changes (0 means
  // the email was already there), which is how we know whether to skip the
  // welcome email and tell the visitor they're already subscribed.
  const insertResult = await env.DB.prepare(
    `INSERT OR IGNORE INTO subscribers (id, email, created_at)
     VALUES (?, ?, datetime('now'))`
  ).bind(crypto.randomUUID(), email).run();

  const alreadySubscribed = !(insertResult.meta && insertResult.meta.changes > 0);

  if (!alreadySubscribed && env.RESEND_API_KEY) {
    try {
      await fetch("https://api.resend.com/emails", {
        method: "POST",
        headers: {
          Authorization: `Bearer ${env.RESEND_API_KEY}`,
          "Content-Type": "application/json",
        },
        body: JSON.stringify({
          from: env.RESEND_FROM || "FlugVel <onboarding@resend.dev>",
          to: email,
          subject: WELCOME_EMAIL_SUBJECT,
          text: WELCOME_EMAIL_TEXT,
          html: WELCOME_EMAIL_HTML,
        }),
      });
    } catch {
      // Best-effort - see the function-level comment above, the subscriber
      // is already saved in D1 either way.
    }
  }

  return json({ ok: true, alreadySubscribed });
}
