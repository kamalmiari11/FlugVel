import { signJWT } from "../_lib/jwt.js";
import { SESSION_COOKIE, json } from "../_lib/auth.js";

// Failed logins beyond this many within WINDOW_MINUTES lock that IP out for
// LOCKOUT_MINUTES. There's a single shared admin password and no other
// throttling in front of this endpoint, so without this an attacker with
// the login URL could guess indefinitely. Numbers are generous for the
// legitimate case (a typo or two) while making brute-forcing impractical.
const MAX_ATTEMPTS = 5;
const WINDOW_MINUTES = 15;
const LOCKOUT_MINUTES = 15;

// POST /api/login  { password: string }
// Checks the password against the ADMIN_PASSWORD secret and, on success,
// sets an httpOnly JWT cookie. There's no separate "username" — this is a
// single-admin dashboard, matching what was asked for (simple but protected).
export async function onRequestPost(context) {
  const { request, env } = context;

  if (!env.ADMIN_PASSWORD || !env.JWT_SECRET) {
    return json({ error: "Server not configured (missing ADMIN_PASSWORD or JWT_SECRET)" }, { status: 500 });
  }

  // Cloudflare sets this on every request that reaches a Pages Function -
  // it's the actual client IP, not something a caller can spoof by setting
  // their own header. Falls back to a shared bucket if it's ever missing
  // (e.g. local dev), which just means everyone shares one rate limit
  // rather than the feature silently doing nothing.
  const ip = request.headers.get("CF-Connecting-IP") || "unknown";

  // Rate limiting is best-effort: if D1 isn't bound or hiccups, the admin
  // should still be able to log in rather than getting locked out by an
  // infra problem. Every DB call below is wrapped accordingly.
  if (env.DB) {
    try {
      const locked = await env.DB.prepare(
        `SELECT 1 FROM login_attempts
         WHERE ip = ? AND locked_until IS NOT NULL AND locked_until > datetime('now')`
      ).bind(ip).first();
      if (locked) {
        return json({ error: "Too many attempts. Try again in a few minutes." }, { status: 429 });
      }
    } catch {
      // Fall through and check the password normally.
    }
  }

  let body;
  try {
    body = await request.json();
  } catch {
    return json({ error: "Invalid request body" }, { status: 400 });
  }

  const password = body && body.password;
  if (!password || password !== env.ADMIN_PASSWORD) {
    if (env.DB) {
      try {
        // One UPSERT handles all three cases: no prior failures (insert),
        // a stale window (reset the count to 1), or an active window
        // (increment, and set locked_until once the threshold is hit).
        // Doing this as a single statement avoids a read-then-write race
        // between two near-simultaneous failed attempts from the same IP.
        await env.DB.prepare(
          `INSERT INTO login_attempts (ip, fail_count, first_fail_at, locked_until)
           VALUES (?, 1, datetime('now'), NULL)
           ON CONFLICT(ip) DO UPDATE SET
             fail_count = CASE
               WHEN first_fail_at <= datetime('now', '-${WINDOW_MINUTES} minutes') THEN 1
               ELSE fail_count + 1
             END,
             first_fail_at = CASE
               WHEN first_fail_at <= datetime('now', '-${WINDOW_MINUTES} minutes') THEN datetime('now')
               ELSE first_fail_at
             END,
             locked_until = CASE
               WHEN first_fail_at <= datetime('now', '-${WINDOW_MINUTES} minutes') THEN NULL
               WHEN fail_count + 1 >= ${MAX_ATTEMPTS} THEN datetime('now', '+${LOCKOUT_MINUTES} minutes')
               ELSE locked_until
             END`
        ).bind(ip).run();
      } catch {
        // Best-effort - see the comment above. The wrong-password response
        // below still goes out either way.
      }
    }

    // Same message either way — don't reveal whether the field was empty
    // vs. wrong, no point giving that away.
    return json({ error: "Incorrect password" }, { status: 401 });
  }

  if (env.DB) {
    try {
      await env.DB.prepare(`DELETE FROM login_attempts WHERE ip = ?`).bind(ip).run();
    } catch {
      // Not worth failing the login over - it just means this IP's slate
      // doesn't get wiped until its window naturally expires.
    }
  }

  const token = await signJWT({ sub: "admin" }, env.JWT_SECRET);

  const headers = new Headers({ "Content-Type": "application/json" });
  headers.append(
    "Set-Cookie",
    `${SESSION_COOKIE}=${token}; HttpOnly; Secure; SameSite=Strict; Path=/; Max-Age=43200`
  );

  return new Response(JSON.stringify({ ok: true }), { status: 200, headers });
}
