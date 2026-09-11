import { json } from "../_lib/auth.js";

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
// There's no unsubscribe link in the welcome email yet - Resend's own
// suppression-list handling needs a verified sending domain (see
// RESEND_FROM below). For a personal-project mailing list, "reply to this
// email and I'll remove you" is a fine stand-in until that's set up; if
// this list grows past that, revisit before sending anything beyond the
// welcome email.
export async function onRequestPost(context) {
  const { request, env } = context;

  // TEMPORARY, remove once the Resend integration is confirmed working:
  // ?debug=1 adds a `resend` field to the response describing what
  // happened on the Resend side - never the API key or email content,
  // just whether it was configured, attempted, and what Resend said back.
  // Gated behind a query param rather than always-on so ordinary visitors
  // never see it.
  const debug = new URL(request.url).searchParams.get("debug") === "1";
  const debugInfo = { hasKey: false, attempted: false, resendOk: null, status: null, error: null };

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

  debugInfo.hasKey = !!env.RESEND_API_KEY;

  if (env.RESEND_API_KEY) {
    debugInfo.attempted = true;
    try {
      const resendRes = await fetch("https://api.resend.com/emails", {
        method: "POST",
        headers: {
          Authorization: `Bearer ${env.RESEND_API_KEY}`,
          "Content-Type": "application/json",
        },
        // RESEND_FROM defaults to Resend's own shared sandbox address,
        // which works with zero setup but reads as coming from Resend, not
        // FlugVel. Set RESEND_FROM (e.g. "FlugVel <updates@flugvel.com>")
        // once flugvel.com is verified as a sending domain in the Resend
        // dashboard - see DEPLOY.md.
        body: JSON.stringify({
          from: env.RESEND_FROM || "FlugVel <onboarding@resend.dev>",
          to: email,
          subject: "You're on the list",
          text:
            "Thanks for signing up. You'll get an email whenever there's a " +
            "real update to FlugVel - new firmware, new features, that " +
            "kind of thing. No spam, and no set schedule.\n\n" +
            "Didn't mean to sign up, or want off the list? Just reply to " +
            "this email and I'll remove you.",
        }),
      });
      debugInfo.resendOk = resendRes.ok;
      debugInfo.status = resendRes.status;
      if (!resendRes.ok) {
        // Resend's error bodies are small JSON objects like
        // {"message": "...", "name": "..."} - safe to surface in debug
        // mode, no secrets in there.
        debugInfo.error = await resendRes.text().catch(() => null);
      }
    } catch (err) {
      // Best-effort in normal operation - see the function-level comment
      // above, the subscriber is already saved in D1 either way. In debug
      // mode, surface what actually went wrong instead of swallowing it.
      debugInfo.error = String(err && err.message ? err.message : err);
    }
  }

  return json(debug ? { ok: true, resend: debugInfo } : { ok: true });
}
