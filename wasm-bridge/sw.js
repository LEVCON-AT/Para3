// =============================================================================
//  PARA-3 :: Service Worker  (Phase B2)
//
//  Precaches the full app on install: HTML shell + audio glue + worklet + ring
//  + port + wasm + icons + manifest. Serves cache-first so the installed PWA
//  works fully offline (Flugmodus).
//
//  Versioning: bump CACHE_VER on any asset change. The activate handler purges
//  older caches; the install handler precaches the new one. clients.claim()
//  means the new SW takes over open tabs without a reload (cleaner upgrades).
//
//  Note: COOP/COEP/SAB are NOT available on an offline PWA. The control path
//  uses Para3Port (postMessage) — see para3-port.js. The audio path is wasm in
//  the worklet and needs no SAB.
// =============================================================================

const CACHE_VER  = 'para3-v28'; // Sprint 4: P3 VK-BRASS @ 105 BPM (UNISON, ARP UP+HOLD ×2, Cm9 latched, MOTION-REC/DETUNE smooth)
const PRECACHE   = [
  './',
  './index.html',
  './para3-responsive.html',
  './documentation.html',
  './conversation.html',
  './para3-audio.js',
  './para3-ring.js',
  './para3-port.js',
  './para3-worklet.js',
  './para3.wasm',
  './manifest.webmanifest',
  './brand/para3-mark.svg',
  './brand/para3-icon-maskable.svg',
  './brand/para3-logo.svg',
];

self.addEventListener('install', (e) => {
  e.waitUntil((async () => {
    const cache = await caches.open(CACHE_VER);
    // addAll is atomic — if any asset fails, the install fails and the old SW
    // (if any) keeps serving. We *want* that: a half-installed cache lies.
    await cache.addAll(PRECACHE);
    await self.skipWaiting();
  })());
});

self.addEventListener('activate', (e) => {
  e.waitUntil((async () => {
    const keys = await caches.keys();
    await Promise.all(keys
      .filter((k) => k !== CACHE_VER)
      .map((k) => caches.delete(k)));
    await self.clients.claim();
  })());
});

self.addEventListener('fetch', (e) => {
  const req = e.request;
  if (req.method !== 'GET') return;

  // Only intercept same-origin GETs we know how to serve from cache.
  const url = new URL(req.url);
  if (url.origin !== self.location.origin) return;

  e.respondWith((async () => {
    const cache = await caches.open(CACHE_VER);
    const cached = await cache.match(req, { ignoreSearch: true });
    if (cached) return cached;

    // Cache-first with a network fallback for assets not in the precache
    // (e.g. dev-time HMR or future additions). Failures fall through to a
    // friendly offline response for navigations.
    try {
      const resp = await fetch(req);
      if (resp.ok && resp.type === 'basic') {
        try { await cache.put(req, resp.clone()); } catch (_) { /* opaque */ }
      }
      return resp;
    } catch (_) {
      if (req.mode === 'navigate') {
        const shell = await cache.match('./index.html');
        if (shell) return shell;
      }
      return new Response('offline', { status: 503 });
    }
  })());
});
