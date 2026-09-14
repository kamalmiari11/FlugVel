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
// Skipped: server-side rate limiting (subscribe.js's IP+D1 throttle). The
// honeypot below covers the common bot case, and a private inbox getting
// occasionally spammed is a much smaller problem than a public mailing
// list's sending quota getting burned - add a throttle here if that
// changes.
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
        subject: SUPPORT_CONFIRMATION_SUBJECT,
        text: supportConfirmationText(name, message),
        html: supportConfirmationHtml(name, message),
      }),
    });
  } catch {
    // Best-effort - see comment above.
  }

  return json({ ok: true });
}
