// dashboard.html was renamed to m-223d0f.html (see the earlier commit
// "Make the login/monitor pages unguessable...") - nothing legitimate
// should ever be served at /dashboard or /dashboard.html again. See
// login.js right next to this file for the full reasoning.
export async function onRequest() {
  return new Response("Not found", { status: 404 });
}
