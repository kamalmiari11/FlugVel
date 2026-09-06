// Cloudflare Pages transparently resolves an extension-less path to its
// .html file by default (a bare /login would otherwise silently serve
// login.html even though it's never linked anywhere in the site's nav -
// see script.js's arrow-key sequence, the only real way in). A Function
// at this exact path runs before that fallback ever gets a chance to, so
// this always 404s regardless of the platform's default behavior. Only
// the literal /login.html URL keeps working.
export async function onRequest() {
  return new Response("Not found", { status: 404 });
}
