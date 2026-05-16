// =============================================================================
//  PARA-3 :: tiny PNG icon generator (no deps)  (Phase B2)
//
//  Generates icon-192.png, icon-512.png, icon-maskable-512.png for the PWA
//  manifest. Pure Node — uses zlib.deflateSync and a hand-rolled PNG chunk
//  writer. Visual: dark background with an orange wave + the "P3" wordmark.
// =============================================================================
import { writeFileSync } from 'node:fs';
import { deflateSync }  from 'node:zlib';

const CRCTAB = new Uint32Array(256);
for (let n = 0; n < 256; n++) {
  let c = n;
  for (let k = 0; k < 8; k++) c = (c & 1) ? (0xedb88320 ^ (c >>> 1)) : (c >>> 1);
  CRCTAB[n] = c;
}
function crc32(buf) {
  let c = 0xffffffff;
  for (let i = 0; i < buf.length; i++)
    c = CRCTAB[(c ^ buf[i]) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}
function chunk(type, data) {
  const out = Buffer.alloc(8 + data.length + 4);
  out.writeUInt32BE(data.length, 0);
  out.write(type, 4, 'ascii');
  data.copy(out, 8);
  out.writeUInt32BE(crc32(out.subarray(4, 8 + data.length)), 8 + data.length);
  return out;
}

// minimal 5x7 pixel font for "P3" — used to brand the icon at any size
const GLYPH = {
  'P': [
    '11110',
    '10001',
    '10001',
    '11110',
    '10000',
    '10000',
    '10000',
  ],
  '3': [
    '11110',
    '00001',
    '00001',
    '01110',
    '00001',
    '00001',
    '11110',
  ],
};

function makePNG(size, opts = {}) {
  const padMask = !!opts.maskablePadding;   // safe zone for maskable icons
  // colors: orange #e0a14b, dark #15171c
  const ORG = [0xe0, 0xa1, 0x4b];
  const DRK = [0x15, 0x17, 0x1c];

  // pixel buffer: each row prefixed by 1 filter byte, then 3 bytes per pixel
  const stride = 1 + size * 3;
  const raw = Buffer.alloc(size * stride);
  for (let y = 0; y < size; y++) {
    const off0 = y * stride;
    raw[off0] = 0;
    for (let x = 0; x < size; x++) {
      const o = off0 + 1 + x * 3;
      raw[o]     = DRK[0];
      raw[o + 1] = DRK[1];
      raw[o + 2] = DRK[2];
    }
  }

  // -- orange sine band crossing the centre --
  const band = Math.max(2, Math.floor(size * 0.045));
  for (let x = 0; x < size; x++) {
    const t = x / (size - 1);
    const yC = size / 2 + Math.sin(t * Math.PI * 4) * (size * 0.18);
    for (let dy = -band; dy <= band; dy++) {
      const y = Math.round(yC + dy);
      if (y < 0 || y >= size) continue;
      const o = y * stride + 1 + x * 3;
      raw[o]     = ORG[0];
      raw[o + 1] = ORG[1];
      raw[o + 2] = ORG[2];
    }
  }

  // -- "P3" wordmark (centred) --
  // glyph cell: 5 wide x 7 tall, with 1-col gap between letters
  // total = 5 + 1 + 5 = 11 cells wide, 7 tall
  const safe = padMask ? 0.78 : 0.92;     // maskable: leave ~10% safe-zone
  const px   = Math.max(2, Math.floor((size * safe) / 11));
  const totalW = 11 * px;
  const totalH = 7 * px;
  const x0 = Math.floor((size - totalW) / 2);
  const y0 = Math.floor((size - totalH) / 2);
  const cells = ['P', '3'];
  for (let gi = 0; gi < cells.length; gi++) {
    const G = GLYPH[cells[gi]];
    const gx0 = x0 + gi * 6 * px;
    for (let gy = 0; gy < 7; gy++) {
      for (let gxc = 0; gxc < 5; gxc++) {
        if (G[gy][gxc] !== '1') continue;
        for (let py = 0; py < px; py++) {
          for (let pxx = 0; pxx < px; pxx++) {
            const X = gx0 + gxc * px + pxx;
            const Y = y0 + gy * px + py;
            if (X < 0 || X >= size || Y < 0 || Y >= size) continue;
            const o = Y * stride + 1 + X * 3;
            raw[o]     = ORG[0];
            raw[o + 1] = ORG[1];
            raw[o + 2] = ORG[2];
          }
        }
      }
    }
  }

  // -- PNG container --
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(size, 0);
  ihdr.writeUInt32BE(size, 4);
  ihdr.writeUInt8(8,  8);   // bit depth 8
  ihdr.writeUInt8(2,  9);   // colour type 2 = RGB
  ihdr.writeUInt8(0, 10);
  ihdr.writeUInt8(0, 11);
  ihdr.writeUInt8(0, 12);
  const idat = deflateSync(raw, { level: 9 });
  const sig = Buffer.from([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]);
  return Buffer.concat([
    sig,
    chunk('IHDR', ihdr),
    chunk('IDAT', idat),
    chunk('IEND', Buffer.alloc(0)),
  ]);
}

writeFileSync('icon-192.png',          makePNG(192));
writeFileSync('icon-512.png',          makePNG(512));
writeFileSync('icon-maskable-512.png', makePNG(512, { maskablePadding: true }));
console.log('wrote icon-192.png, icon-512.png, icon-maskable-512.png');
