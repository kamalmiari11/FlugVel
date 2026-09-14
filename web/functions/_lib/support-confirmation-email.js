// Content for the "we got your message" email sent back to whoever just
// submitted the Support page's contact form - see welcome-email.js for the
// styling conventions this mirrors (inline styles, <table> layout, no web
// fonts) and why.

export const SUPPORT_CONFIRMATION_SUBJECT = "We got your message";

export const supportConfirmationText = (name, message) =>
  `Thanks${name ? ", " + name : ""} - your message came through.\n\n` +
  "We'll take a look and get back to you as soon as we can - usually " +
  "within a few days, sometimes sooner.\n\n" +
  "For reference, here's what you sent:\n\n" +
  `"${message}"\n\n` +
  "In the meantime, the manual might already answer what you're after: " +
  "https://flugvel.com/manual\n";

export const supportConfirmationHtml = (name, message) => `<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>We got your message</title>
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
              <span style="display:inline-block; font-family:'Courier New', Courier, monospace; font-size:11px; letter-spacing:2px; text-transform:uppercase; color:#c05a1e; font-weight:700;">Message received</span>
            </td>
          </tr>
          <tr>
            <td style="padding:10px 32px 0 32px; text-align:center;">
              <h1 style="margin:0; font-family:Georgia, 'Times New Roman', serif; font-weight:normal; font-size:24px; line-height:1.3; color:#23271d;">Thanks${name ? `, ${escapeHtml(name)}` : ""}.</h1>
            </td>
          </tr>
          <tr>
            <td style="padding:18px 32px 0 32px;">
              <p style="margin:0; font-family:Arial, Helvetica, sans-serif; font-size:15px; line-height:1.6; color:#5c6249;">We'll take a look and get back to you as soon as we can &mdash; usually within a few days, sometimes sooner.</p>
            </td>
          </tr>
          <tr>
            <td style="padding:22px 32px 0 32px;">
              <table role="presentation" width="100%" cellpadding="0" cellspacing="0" style="background-color:#eef1e4; border-left:3px solid #c05a1e; border-radius:0 6px 6px 0;">
                <tr>
                  <td style="padding:14px 18px;">
                    <p style="margin:0; font-family:Arial, Helvetica, sans-serif; font-size:13.5px; line-height:1.6; color:#5c6249; white-space:pre-wrap;">${escapeHtml(message)}</p>
                  </td>
                </tr>
              </table>
            </td>
          </tr>
          <tr>
            <td style="padding:24px 32px 28px 32px; text-align:center;">
              <a href="https://flugvel.com/manual" style="display:inline-block; background-color:#c05a1e; color:#faf9f1; font-family:Arial, Helvetica, sans-serif; font-size:14px; font-weight:700; text-decoration:none; padding:12px 28px; border-radius:5px;">Browse the manual</a>
            </td>
          </tr>
        </table>
      </td>
    </tr>
  </table>
</body>
</html>
`;

// Minimal HTML-escaping for the two fields (name, message) that come
// straight from the visitor and get interpolated into the template above -
// everything else in the markup is a fixed literal, so this is the only
// injection surface that needs it.
function escapeHtml(s) {
  return String(s)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;")
    .replace(/'/g, "&#39;");
}
