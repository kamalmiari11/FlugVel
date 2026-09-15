import { json } from "../_lib/auth.js";
import { SUPPORT_CONFIRMATION_SUBJECT, supportConfirmationText, supportConfirmationHtml } from "../_lib/support-confirmation-email.js";
import { supportNotificationSubject, supportNotificationText, supportNotificationHtml } from "../_lib/support-notification-email.js";

// A deliberately loose but real email check - see subscribe.js for why.
const EMAIL_RE = /^[^\s@]+@[^\s@]+\.[^\s@]+$/;

// Where a submitted support message actually goes. Not a secret, so it's a
// plain constant rather than an env var - same reasoning main.cpp's own
// CHECKIN_URL/DEVICE_API_KEY constants use for single-deployment specifics
// that don't need to change without a code change anyway.
const SUPPORT_TO = "kml@flugvel.com";

// POST /api/support  { name?: string, email: string, message: string, company?: string }
// Public endpoint (no login needed) for the Support page's contact form.
// Unlike /api/subscribe, this never touches D1 - the destination inbox IS
// the record; there's nothing here worth keeping a second copy of.
//
// Rate limited per IP (same D1 throttle as subscribe.js, keyed "support:"
// so the two forms don't share a budget). Every submission sends an email
// to an address the visitor typed, so without a cap the form is a free way
// to hammer a stranger's inbox from flugvel.com - which is exactly what
// gets a sending domain's reputation burned.
const RATE_LIMIT_MAX = 3;
const RATE_LIMIT_WINDOW_MINUTES = 10;

// Replies to the confirmation go to a real, monitored inbox. A no-reply
// sender that bounces replies is itself a spam signal.
const REPLY_TO = SUPPORT_TO;

export async function onRequestPost(context) {
  const { request, env } = context;

  let body;
  try {
    body = await request.json();
  } catch {
    return json({ error: "Invalid request body" }, { status: 400 });
  }

  // Honeypot - same bot-defense idea as subscribe.js's "company" field, but
  // deliberately NOT named after a real autofill category this time (see
  // support.html's comment on the input) - a browser auto-filling this
  // from a saved address profile was silently turning real submissions
  // into ones the server treats as bot traffic.
  if (body && typeof body.hp_note === "string" && body.hp_note.trim() !== "") {
    return json({ ok: true });
  }

  const name = ((body && body.name) || "").trim().slice(0, 100);
  const email = ((body && body.email) || "").trim().toLowerCase();
  const message = ((body && body.message) || "").trim();

  if (!email || email.length > 320 || !EMAIL_RE.test(email)) {
    return json({ error: "Enter a valid email address" }, { status: 400 });
  }
  if (!message || message.length > 4000) {
    return json({ error: message ? "Message is too long" : "Enter a message" }, { status: 400 });
  }

  if (!env.RESEND_API_KEY) {
    return json({ error: "Server not configured for sending" }, { status: 500 });
  }

  if (env.DB) {
    const key = "support:" + (request.headers.get("CF-Connecting-IP") || "unknown");
    try {
      const attempts = await env.DB.prepare(
        `SELECT count FROM subscribe_attempts
         WHERE ip = ? AND window_start > datetime('now', '-${RATE_LIMIT_WINDOW_MINUTES} minutes')`
      ).bind(key).first();
      if (attempts && attempts.count >= RATE_LIMIT_MAX) {
        return json({ error: "Too many messages from this connection. Try again later." }, { status: 429 });
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
      ).bind(key).run();
    } catch {
      // Best-effort, same as subscribe.js - a throttle glitch never blocks a real message.
    }
  }

  try {
    const res = await fetch("https://api.resend.com/emails", {
      method: "POST",
      headers: {
        Authorization: `Bearer ${env.RESEND_API_KEY}`,
        "Content-Type": "application/json",
      },
      body: JSON.stringify({
        from: env.RESEND_FROM || "FlugVel <onboarding@resend.dev>",
        to: SUPPORT_TO,
        // So replying in the inbox goes straight back to whoever wrote in,
        // not to the FlugVel sending address.
        reply_to: email,
        subject: supportNotificationSubject(name, email),
        text: supportNotificationText(name, email, message),
        html: supportNotificationHtml(name, email, message),
      }),
    });
    if (!res.ok) {
      return json({ error: "Couldn't send right now - try again in a bit" }, { status: 502 });
    }
  } catch {
    return json({ error: "Couldn't send right now - try again in a bit" }, { status: 502 });
  }

  // Best-effort confirmation back to whoever wrote in, same reasoning
  // subscribe.js's welcome email uses: the message to SUPPORT_TO above is
  // what actually matters and has already succeeded, so a failure here
  // (Resend hiccup, whatever) shouldn't turn a successful submission into
  // an error shown to the visitor.
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
        reply_to: REPLY_TO,
        subject: SUPPORT_CONFIRMATION_SUBJECT,
        text: supportConfirmationText(),
        html: supportConfirmationHtml(),
        headers: {
          // RFC 3834: marks this as an automatic reply, so other
          // autoresponders don't answer it (no mail loops) and filters
          // treat it as the transactional message it is.
          "Auto-Submitted": "auto-replied",
        },
      }),
    });
  } catch {
    // Best-effort - see comment above.
  }

  return json({ ok: true });
}
