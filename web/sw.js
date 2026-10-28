// Офлайн-режим сайта: при первом заходе все файлы сайта (страница, скрипты, ядро WebAssembly,
// каталоги плитки) кладутся в кэш браузера, дальше сайт открывается и без интернета. Страница — из
// сети, если она есть (так приходят обновления), иначе из кэша; остальное — из кэша: у файлов в
// адресе метка сборки (web/build.sh), так что новая сборка — новые адреса.
//
// Список файлов и метку подставляет web/build.sh; без сборки (разработка) — кэш пустой и всё из сети.

const BUILD = 'dev';
const FILES = [];
const CACHE = `tilelayout-${BUILD}`;

self.addEventListener('install', (event) => {
  event.waitUntil(caches.open(CACHE).then((cache) => cache.addAll(FILES)).then(() => self.skipWaiting()));
});

self.addEventListener('activate', (event) => {
  event.waitUntil((async () => {
    for (const name of await caches.keys()) { // прошлые сборки
      if (name.startsWith('tilelayout-') && name !== CACHE) await caches.delete(name);
    }
    await self.clients.claim();
  })());
});

self.addEventListener('fetch', (event) => {
  const request = event.request;
  if (request.method !== 'GET' || new URL(request.url).origin !== self.location.origin) return;
  if (request.mode === 'navigate') { // страница: свежая из сети, без сети — из кэша
    event.respondWith((async () => {
      const path = new URL(request.url).pathname;
      const main = path === new URL('./', self.location).pathname || path.endsWith('/index.html');
      try {
        const response = await fetch(request);
        if (response.ok && main) { // свежая главная — на случай, когда сети не будет
          const cache = await caches.open(CACHE);
          await cache.put('./', response.clone());
        }
        return response;
      } catch {
        const cached = await caches.match(request, { ignoreSearch: true });
        return cached || (main && (await caches.match('./'))) || Response.error();
      }
    })());
    return;
  }
  event.respondWith((async () => {
    const cached = await caches.match(request);
    if (cached) return cached;
    const response = await fetch(request);
    if (response.ok && BUILD !== 'dev') { // картинки каталогов и прочее, чего не было в списке
      const cache = await caches.open(CACHE);
      await cache.put(request, response.clone());
    }
    return response;
  })());
});
