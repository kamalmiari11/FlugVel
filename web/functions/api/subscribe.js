import { json } from "../_lib/auth.js";
import { WELCOME_EMAIL_SUBJECT, WELCOME_EMAIL_TEXT, WELCOME_EMAIL_HTML } from "../_lib/welcome-email.js";

// A deliberately loose but real email check - not trying to fully validate
// RFC 5322, just catching "clearly not an email" (empty, no @, no dot after
// the @) without rejecting anything a real address might legitimately look
// like (plus-addressing, subdomains, etc).
const EMAIL_RE = /^[^\s@]+@[^\s@]+\.[^\s@]+$/;

// POST /api/subscribe  { email: string }
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

  const email = ((body && body.email) || "").trim().toLowerCase();
  if (!email || email.length > 320 || !EMAIL_RE.test(email)) {
    return json({ error: "Enter a valid email address" }, { status: 400 });
  }

  if (!env.DB) {
    return json({ error: "Server not configured (missing DB binding)" }, { status: 500 });
  }

  // INSERT OR IGNORE: signing up twice - or an address already on the
  // list - is a quiet no-op. Deliberately doesn't reveal to the caller
  // whether that email was already subscribed.
  await env.DB.prepare(
    `INSERT OR IGNORE INTO subscribers (id, email, created_at)
     VALUES (?, ?, datetime('now'))`
  ).bind(crypto.randomUUID(), email).run();

  if (env.RESEND_API_KEY) {
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

  return json({ ok: true });
}
