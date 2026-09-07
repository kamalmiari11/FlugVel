// login.html was renamed to p-f838d1.html (see the earlier commit
// "Make the login/monitor pages unguessable...") - nothing legitimate
// should ever be served at /login or /login.html again. A Function at
// this exact path always wins over static-asset resolution (including
// Cloudflare's default extension/clean-URL handling), so this reliably
// 404s regardless of any platform-level fallback behavior.
export async function onRequest() {
  return new Response("Not found", { status: 404 });
}
