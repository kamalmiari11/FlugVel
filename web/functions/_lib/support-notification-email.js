// Content for the notification sent to SUPPORT_TO when someone submits the
// Support page's contact form - see welcome-email.js for the styling
// conventions this mirrors (inline styles, <table> layout, no web fonts)
// and why.

export const supportNotificationSubject = (name, email) => `Support message from ${name || email}`;

export const supportNotificationText = (name, email, message) =>
  `From: ${name ? `${name} <${email}>` : email}\n\n${message}\n`;

export const supportNotificationHtml = (name, email, message) => `<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Support message</title>
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
              <span style="display:inline-block; font-family:'Courier New', Courier, monospace; font-size:11px; letter-spacing:2px; text-transform:uppercase; color:#c05a1e; font-weight:700;">Support message</span>
            </td>
          </tr>
          <tr>
            <td style="padding:10px 32px 0 32px; text-align:center;">
              <h1 style="margin:0; font-family:Georgia, 'Times New Roman', serif; font-weight:normal; font-size:24px; line-height:1.3; color:#23271d;">${escapeHtml(name || email)}</h1>
            </td>
          </tr>
          <tr>
            <td style="padding:18px 32px 0 32px;">
              <p style="margin:0; font-family:Arial, Helvetica, sans-serif; font-size:14px; line-height:1.6; color:#5c6249;">Reply to this email to reply straight to them &mdash; Reply-To is already set to <a href="mailto:${escapeHtml(email)}" style="color:#c05a1e;">${escapeHtml(email)}</a>.</p>
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
            <td style="padding:28px 32px 28px 32px;"></td>
          </tr>
        </table>
      </td>
    </tr>
  </table>
</body>
</html>
`;

// Minimal HTML-escaping for the visitor-supplied fields interpolated above -
// see support-confirmation-email.js's own copy of this for why.
function escapeHtml(s) {
  return String(s)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;")
    .replace(/'/g, "&#39;");
}
