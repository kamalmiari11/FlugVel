// Generates the 16x16 cardinal plane sprites in src/ui/PlaneSprite.cpp.
// Run with: node tools/gen_plane_sprite_16.js
// The east-facing silhouette below is the single source of truth; N/S/W are
// exact 90-degree rotations of it, so all four are guaranteed consistent.
const EAST = [
  '......##........',
  '......##........',
  '.....###........',
  '.....####.......',
  '.....#####......',
  '..#..#####......',
  '..##.######.....',
  '..#############.',
  '..#############.',
  '..##.######.....',
  '..#..#####......',
  '.....#####......',
  '.....####.......',
  '.....###........',
  '......##........',
  '......##........',
];

const N = 16;
const grid = (rows) => rows.map(r => r.split('').map(ch => ch === '#' ? 1 : 0));

// Rotate 90 degrees counter-clockwise on screen: a pixel at the right edge
// (nose, facing east) ends up at the top edge (nose, facing north).
function rotCCW(g) {
  const out = Array.from({ length: N }, () => new Array(N).fill(0));
  for (let r = 0; r < N; r++)
    for (let c = 0; c < N; c++)
      out[N - 1 - c][r] = g[r][c];
  return out;
}

function toBytes(g) {
  const rows = [];
  for (let r = 0; r < N; r++) {
    let b0 = 0, b1 = 0;
    for (let c = 0; c < 8; c++) if (g[r][c]) b0 |= 1 << (7 - c);
    for (let c = 8; c < 16; c++) if (g[r][c]) b1 |= 1 << (15 - c);
    rows.push([b0, b1]);
  }
  return rows;
}

const hex = (v) => '0x' + v.toString(16).toUpperCase().padStart(2, '0');

function emit(name, g) {
  const bytes = toBytes(g);
  let out = `static const uint8_t planeSmall${name}[32] PROGMEM = {\n`;
  for (let r = 0; r < N; r += 4) {
    out += '    ' + [0, 1, 2, 3].map(i => {
      const [b0, b1] = bytes[r + i];
      return `${hex(b0)},${hex(b1)},`;
    }).join(' ') + '\n';
  }
  return out + '};\n';
}

const east = grid(EAST);
const north = rotCCW(east);
const west = rotCCW(north);
const south = rotCCW(west);

for (const [name, g] of [['N', north], ['E', east], ['S', south], ['W', west]]) {
  console.log(`--- ${name} ---`);
  console.log(g.map(row => row.map(v => v ? '#' : '.').join('')).join('\n'));
  console.log(emit(name, g));
}
