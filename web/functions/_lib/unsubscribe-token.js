// Signed unsubscribe tokens.
//
// An unsubscribe link has to identify an address to a stranger holding only
// the URL, which rules out putting the address in the query string on its
// own: anyone could then unsubscribe anyone else by editing it, and could
// walk the list by guessing addresses. So the link carries the address plus
// an HMAC of it, and the endpoint refuses anything whose signature doesn't
// match.
//
// HS256 over JWT_SECRET, the same secret and the same Web Crypto primitives
// the admin session tokens already use (see jwt.js) - this is deliberately
// NOT a JWT, though: there is no expiry. An unsubscribe link has to keep
// working in a two-year-old email sitting in someone's archive, which is
// exactly the case a JWT's `exp` exists to prevent.

function base64url(bytes) {
  let binary = "";
  const arr = new Uint8Array(bytes);
  for (let i = 0; i < arr.length; i++) binary += String.fromCharCode(arr[i]);
  return btoa(binary).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
}

function base64urlToBytes(str) {
  str = str.replace(/-/g, "+").replace(/_/g, "/");
  while (str.length % 4) str += "=";
  const binary = atob(str);
  const bytes = new Uint8Array(binary.length);
  for (let i = 0; i < binary.length; i++) bytes[i] = binary.charCodeAt(i);
  return bytes;
}

async function hmacKey(secret) {
  return crypto.subtle.importKey(
    "raw",
    new TextEncoder().encode(secret),
    { name: "HMAC", hash: "SHA-256" },
    false,
    ["sign", "verify"]
  );
}

// "<base64url(email)>.<base64url(hmac)>" - one opaque blob, so the link has a
// single parameter to pass around rather than an address and a signature that
// could get separated.
export async function makeUnsubscribeToken(email, secret) {
  const normalized = String(email).trim().toLowerCase();
  const key = await hmacKey(secret);
  const sig = await crypto.subtle.sign("HMAC", key, new TextEncoder().encode(normalized));
  return `${base64url(new TextEncoder().encode(normalized))}.${base64url(sig)}`;
}

// Returns the address the token vouches for, or null if it is malformed or
// the signature doesn't match. Never throws - this runs on input straight
// from a URL typed, forwarded or mangled by anyone.
export async function readUnsubscribeToken(token, secret) {
  try {
    if (typeof token !== "string") return null;
    const dot = token.indexOf(".");
    if (dot <= 0 || dot === token.length - 1) return null;

    const emailBytes = base64urlToBytes(token.slice(0, dot));
    const email = new TextDecoder().decode(emailBytes);
    const sig = base64urlToBytes(token.slice(dot + 1));

    const key = await hmacKey(secret);
    // crypto.subtle.verify is constant-time, which matters here: comparing
    // signatures with === would leak, byte by byte through timing, enough to
    // forge one.
    const ok = await crypto.subtle.verify("HMAC", key, sig, new TextEncoder().encode(email));
    return ok ? email : null;
  } catch {
    return null;
  }
}

// The full link to put in an email. origin comes from the incoming request
// rather than a configured constant, so this works unchanged on a preview
// deployment, a custom domain, or localhost during development.
export async function unsubscribeUrl(origin, email, secret) {
  const token = await makeUnsubscribeToken(email, secret);
  return `${origin}/unsubscribe?t=${encodeURIComponent(token)}`;
}
