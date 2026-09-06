import { verifyJWT } from "./jwt.js";

export const SESSION_COOKIE = "flugvel_session";

export function getCookie(request, name) {
  const header = request.headers.get("Cookie") || "";
  const parts = header.split(";");
  for (const part of parts) {
    const [key, ...rest] = part.trim().split("=");
    if (key === name) return decodeURIComponent(rest.join("="));
  }
  return null;
}

// Reads the session cookie off a Pages Function's request and validates it.
// Returns the JWT payload if the caller is logged in, or null otherwise —
// callers should treat null as "respond 401", never as "treat as guest".
export async function requireAuth(context) {
  const token = getCookie(context.request, SESSION_COOKIE);
  if (!token) return null;
  if (!context.env.JWT_SECRET) {
    // Misconfiguration guard: without a secret set, nothing can be trusted.
    return null;
  }
  return verifyJWT(token, context.env.JWT_SECRET);
}

export function unauthorized() {
  return new Response(JSON.stringify({ error: "Unauthorized" }), {
    status: 401,
    headers: { "Content-Type": "application/json" },
  });
}

export function json(data, init = {}) {
  return new Response(JSON.stringify(data), {
    ...init,
    headers: { "Content-Type": "application/json", ...(init.headers || {}) },
  });
}
