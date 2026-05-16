// =============================================================================
//  PARA-3 :: static server with COOP/COEP
//
//  SharedArrayBuffer (the control ring) requires a cross-origin-isolated page:
//  COOP: same-origin + COEP: require-corp. Also serves .wasm with the correct
//  MIME so WebAssembly.compileStreaming works.
//
//  run: node serve.mjs [dir] [port]
// =============================================================================
import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
import { extname, join, normalize } from 'node:path';

const ROOT = process.argv[2] || '.';
const PORT = parseInt(process.argv[3] || '8080', 10);

const MIME = {
  '.html':         'text/html; charset=utf-8',
  '.js':           'text/javascript; charset=utf-8',
  '.mjs':          'text/javascript; charset=utf-8',
  '.wasm':         'application/wasm',
  '.json':         'application/json',
  '.css':          'text/css; charset=utf-8',
  '.png':          'image/png',
  '.svg':          'image/svg+xml',
  '.webmanifest':  'application/manifest+json',
};

const server = createServer(async (req, res) => {
  // headers required for cross-origin isolation -> SharedArrayBuffer
  res.setHeader('Cross-Origin-Opener-Policy', 'same-origin');
  res.setHeader('Cross-Origin-Embedder-Policy', 'require-corp');
  res.setHeader('Cross-Origin-Resource-Policy', 'same-origin');

  let p = decodeURIComponent((req.url || '/').split('?')[0]);
  // Production UI at /, the long-form documentation at /documentation (without
  // .html), the conversation log at /conversation, index.html stays accessible
  // as /index.html for the minimal harness.
  if (p === '/') p = '/para3-responsive.html';
  else if (p === '/documentation' || p === '/documentation/') p = '/documentation.html';
  else if (p === '/conversation'  || p === '/conversation/')  p = '/conversation.html';
  const safe = normalize(p).replace(/^(\.\.[/\\])+/, '');
  const file = join(ROOT, safe);
  try {
    const data = await readFile(file);
    res.setHeader('Content-Type', MIME[extname(file)] || 'application/octet-stream');
    res.end(data);
  } catch {
    res.statusCode = 404;
    res.end('not found');
  }
});

server.listen(PORT, () => {
  console.log(`PARA-3 server: http://localhost:${PORT}  (root=${ROOT})`);
  console.log('COOP/COEP set -> SharedArrayBuffer enabled (cross-origin isolated)');
});
