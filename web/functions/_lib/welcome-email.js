// Content for the "you're on the list" email sent from /api/subscribe.
//
// Both parts are functions of the unsubscribe URL rather than constants,
// because that link is signed per address (see _lib/unsubscribe-token.js) -
// there is no one string that works for every recipient.
// Split out of subscribe.js purely so that file stays readable - this one
// is mostly markup, that one is the actual endpoint logic.
//
// Resend wants both `text` and `html` on the same send: `text` is the
// plain-text part (shown by clients that don't render HTML, and used by
// spam filters as a sanity check that it roughly matches the HTML), `html`
// is what most inboxes actually render.

export const WELCOME_EMAIL_SUBJECT = "You're on the list";

export const welcomeEmailText = (unsubUrl) =>
  "Thanks for signing up.\n\n" +
  "You'll get an email whenever there's a real update to FlugVel - new " +
  "firmware, new features, that kind of thing. No spam, and no set " +
  "schedule.\n\n" +
  "Read the manual: https://flugvel.com/manual.html\n\n" +
  "Didn't mean to sign up, or want off the list? Unsubscribe here:\n" +
  unsubUrl + "\n";

// Email HTML is its own dialect - no external stylesheets or web fonts (most
// inboxes strip or ignore them), everything inline, layout done with
// <table> rather than flex/grid (Outlook desktop still renders email with
// Word's engine, which ignores most modern CSS). Fonts fall back to
// whatever's actually installed rather than the site's Inter/IBM Plex Mono.
// Colors match the site's palette (styles.css :root) by value, since email
// clients won't read CSS custom properties.
export const welcomeEmailHtml = (unsubUrl) => `<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>You're on the list</title>
</head>
<body style="margin:0; padding:0; background-color:#c7cfb6;">
  <table role="presentation" width="100%" cellpadding="0" cellspacing="0" style="background-color:#c7cfb6;">
    <tr>
      <td align="center" style="padding:32px 16px;">
        <table role="presentation" width="100%" cellpadding="0" cellspacing="0" style="max-width:520px; background-color:#faf9f1; border-radius:8px;">
          <tr>
            <td style="padding:28px 32px 0 32px; text-align:center;">
              <span style="font-family:'Courier New', Courier, monospace; font-size:13px; letter-spacing:4px; font-weight:700; color:#23271d;">[&nbsp;FLUGVEL&nbsp;]</span>
            </td>
          </tr>
          <tr>
            <td style="padding:22px 32px 0 32px; text-align:center;">
              <span style="display:inline-block; font-family:'Courier New', Courier, monospace; font-size:11px; letter-spacing:2px; text-transform:uppercase; color:#c05a1e; font-weight:700;">You're on the list</span>
            </td>
          </tr>
          <tr>
            <td style="padding:10px 32px 0 32px; text-align:center;">
              <h1 style="margin:0; font-family:Georgia, 'Times New Roman', serif; font-weight:normal; font-size:24px; line-height:1.3; color:#23271d;">Thanks for signing up.</h1>
            </td>
          </tr>
          <tr>
            <td style="padding:18px 32px 0 32px;">
              <p style="margin:0 0 16px 0; font-family:Arial, Helvetica, sans-serif; font-size:15px; line-height:1.6; color:#5c6249;">You'll get an email whenever there's a real update to FlugVel &mdash; new firmware, new features, that kind of thing. No spam, and no set schedule.</p>
              <p style="margin:0; font-family:Arial, Helvetica, sans-serif; font-size:15px; line-height:1.6; color:#5c6249;">In the meantime, here's the manual if you want to see what it actually does.</p>
            </td>
          </tr>
          <tr>
            <td style="padding:24px 32px 8px 32px; text-align:center;">
              <a href="https://flugvel.com/manual.html" style="display:inline-block; background-color:#c05a1e; color:#faf9f1; font-family:Arial, Helvetica, sans-serif; font-size:14px; font-weight:700; text-decoration:none; padding:12px 28px; border-radius:5px;">Read the manual</a>
            </td>
          </tr>
          <tr>
            <td style="padding:28px 32px 28px 32px; border-top:1px solid rgba(35,39,29,.14);">
              <p style="margin:20px 0 0 0; font-family:Arial, Helvetica, sans-serif; font-size:12.5px; line-height:1.6; color:#8a9078; text-align:center;">Didn&rsquo;t mean to sign up, or want off the list? <a href="${unsubUrl}" style="color:#8a9078;">Unsubscribe</a>.</p>
            </td>
          </tr>
        </table>
      </td>
    </tr>
  </table>
</body>
</html>
`;
