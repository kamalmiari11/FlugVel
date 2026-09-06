import { signJWT } from "../_lib/jwt.js";
import { SESSION_COOKIE, json } from "../_lib/auth.js";

// POST /api/login  { password: string }
// Checks the password against the ADMIN_PASSWORD secret and, on success,
// sets an httpOnly JWT cookie. There's no separate "username" — this is a
// single-admin dashboard, matching what was asked for (simple but protected).
export async function onRequestPost(context) {
  const { request, env } = context;

  if (!env.ADMIN_PASSWORD || !env.JWT_SECRET) {
    return json({ error: "Server not configured (missing ADMIN_PASSWORD or JWT_SECRET)" }, { status: 500 });
  }

  let body;
  try {
    body = await request.json();
  } catch {
    return json({ error: "Invalid request body" }, { status: 400 });
  }

  const password = body && body.password;
  if (!password || password !== env.ADMIN_PASSWORD) {
    // Same message either way — don't reveal whether the field was empty
    // vs. wrong, no point giving that away.
    return json({ error: "Incorrect password" }, { status: 401 });
  }

  const token = await signJWT({ sub: "admin" }, env.JWT_SECRET);

  const headers = new Headers({ "Content-Type": "application/json" });
  headers.append(
    "Set-Cookie",
    `${SESSION_COOKIE}=${token}; HttpOnly; Secure; SameSite=Strict; Path=/; Max-Age=43200`
  );

  return new Response(JSON.stringify({ ok: true }), { status: 200, headers });
}
