import { requireAuth, unauthorized, json } from "../_lib/auth.js";

// GET /api/me — lets the dashboard page check "am I logged in?" on load
// without having to try a real data call first.
export async function onRequestGet(context) {
  const auth = await requireAuth(context);
  if (!auth) return unauthorized();
  return json({ authenticated: true });
}
