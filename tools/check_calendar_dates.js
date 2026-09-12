// Ports the date maths from src/api/calendar_api.cpp and checks it against
// JS Date (UTC) as ground truth.

function daysFromCivil(y, m, d) {
  y -= m <= 2 ? 1 : 0;
  const era = Math.floor((y >= 0 ? y : y - 399) / 400);
  const yoe = y - era * 400;
  const doy = Math.floor((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5) + d - 1;
  const doe = yoe * 365 + Math.floor(yoe / 4) - Math.floor(yoe / 100) + doy;
  return era * 146097 + doe - 719468;
}

function civilFromDays(z) {
  z += 719468;
  const era = Math.floor((z >= 0 ? z : z - 146096) / 146097);
  const doe = z - era * 146097;
  const yoe = Math.floor((doe - Math.floor(doe / 1460) + Math.floor(doe / 36524) - Math.floor(doe / 146096)) / 365);
  let yr = yoe + era * 400;
  const doy = doe - (365 * yoe + Math.floor(yoe / 4) - Math.floor(yoe / 100));
  const mp = Math.floor((5 * doy + 2) / 153);
  const dd = doy - Math.floor((153 * mp + 2) / 5) + 1;
  const mm = mp + (mp < 10 ? 3 : -9);
  return { y: yr + (mm <= 2 ? 1 : 0), m: mm, d: dd };
}

const weekdayFromDays = (z) => ((z % 7) + 11) % 7;

function daysInMonth(y, m) {
  if (m === 2) return (y % 4 === 0 && y % 100 !== 0) || y % 400 === 0 ? 29 : 28;
  return [31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31][m - 1];
}

let fails = 0;
const bad = (msg) => { console.log('FAIL ' + msg); fails++; };

// 1. round-trip + weekday + epoch agreement, every day across 25 years
for (let t = Date.UTC(2010, 0, 1); t <= Date.UTC(2035, 0, 1); t += 86400000) {
  const dt = new Date(t);
  const y = dt.getUTCFullYear(), m = dt.getUTCMonth() + 1, d = dt.getUTCDate();
  const days = daysFromCivil(y, m, d);
  if (days !== t / 86400000) bad(`epoch ${y}-${m}-${d}: ${days} vs ${t / 86400000}`);
  const back = civilFromDays(days);
  if (back.y !== y || back.m !== m || back.d !== d) bad(`roundtrip ${y}-${m}-${d} -> ${JSON.stringify(back)}`);
  if (weekdayFromDays(days) !== dt.getUTCDay()) bad(`weekday ${y}-${m}-${d}`);
  if (d === 1 && daysInMonth(y, m) !== new Date(Date.UTC(y, m, 0)).getUTCDate()) bad(`daysInMonth ${y}-${m}`);
}
console.log('date helpers checked across 2010-2035');

// 2. "Nth weekday of month" — the ordinal BYDAY branch.
function nthWeekday(y, m, ordinal, wd) {
  const dim = daysInMonth(y, m);
  if (ordinal > 0) {
    const first = daysFromCivil(y, m, 1);
    const shift = (wd - weekdayFromDays(first) + 7) % 7;
    const target = first + shift + (ordinal - 1) * 7;
    return target > daysFromCivil(y, m, dim) ? null : target;
  }
  const last = daysFromCivil(y, m, dim);
  const shift = (weekdayFromDays(last) - wd + 7) % 7;
  const target = last - shift + (ordinal + 1) * 7;
  return target < daysFromCivil(y, m, 1) ? null : target;
}

// Ground truth by brute force over the month.
function nthWeekdayBrute(y, m, ordinal, wd) {
  const all = [];
  for (let d = 1; d <= daysInMonth(y, m); d++) {
    const days = daysFromCivil(y, m, d);
    if (weekdayFromDays(days) === wd) all.push(days);
  }
  const idx = ordinal > 0 ? ordinal - 1 : all.length + ordinal;
  return idx >= 0 && idx < all.length ? all[idx] : null;
}

for (let y = 2024; y <= 2030; y++)
  for (let m = 1; m <= 12; m++)
    for (let wd = 0; wd < 7; wd++)
      for (const ord of [1, 2, 3, 4, 5, -1, -2]) {
        const a = nthWeekday(y, m, ord, wd), b = nthWeekdayBrute(y, m, ord, wd);
        if (a !== b) bad(`nth ${y}-${m} ord=${ord} wd=${wd}: ${a} vs ${b}`);
      }
console.log('nth-weekday checked for 2024-2030, all ordinals 1..5 and -1,-2');

// 3. Weekly interval: which Mondays does "every 3 weeks" actually hit?
const baseDay = daysFromCivil(2026, 1, 7);   // a Wednesday
const baseWd = weekdayFromDays(baseDay);
const baseMonday = baseDay - ((baseWd + 6) % 7);
const mask = (1 << 2) | (1 << 4);            // TU, TH
const hits = [];
for (let i = 0; i < 60; i++) {
  const day = baseDay + i;
  const wd = weekdayFromDays(day);
  if (!(mask & (1 << wd))) continue;
  const monday = day - ((wd + 6) % 7);
  if (Math.floor((monday - baseMonday) / 7) % 3 !== 0) continue;
  const c = civilFromDays(day);
  hits.push(`${c.y}-${String(c.m).padStart(2, '0')}-${String(c.d).padStart(2, '0')}(${'SunMonTueWedThuFriSat'.substr(wd * 3, 3)})`);
}
console.log('every-3-weeks TU/TH from Wed 2026-01-07:', hits.join(' '));

console.log(fails === 0 ? '\nALL CHECKS PASSED' : `\n${fails} FAILURES`);
