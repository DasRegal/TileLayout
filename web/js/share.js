// Ссылка на проект без сервера: проект (одной строкой, без картинки подложки) сжимается deflate и
// кладётся в адрес сайта после «#p=» (base64url). Часть адреса после «#» на сервер не уходит —
// проект передаётся только тому, кому отправили ссылку. Тот же формат у настольной программы.

const PREFIX = '#p=';

function toBase64Url(bytes) {
  let binary = '';
  for (let i = 0; i < bytes.length; i += 0x8000) binary += String.fromCharCode(...bytes.subarray(i, i + 0x8000));
  return btoa(binary).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
}

function fromBase64Url(text) {
  const binary = atob(text.replace(/-/g, '+').replace(/_/g, '/'));
  const bytes = new Uint8Array(binary.length);
  for (let i = 0; i < binary.length; i++) bytes[i] = binary.charCodeAt(i);
  return bytes;
}

async function pipe(bytes, transform) {
  return new Uint8Array(await new Response(new Blob([bytes]).stream().pipeThrough(transform)).arrayBuffer());
}

/** Ссылка на проект projectText — адрес этой страницы с проектом после «#p=». */
export async function shareLink(projectText) {
  const packed = await pipe(new TextEncoder().encode(projectText), new CompressionStream('deflate-raw'));
  return `${location.origin}${location.pathname}${PREFIX}${toBase64Url(packed)}`;
}

/** Проект из адреса страницы (текст) или null, если в адресе его нет; битая ссылка — исключение. */
export async function projectFromLocation() {
  if (!location.hash.startsWith(PREFIX)) return null;
  const packed = fromBase64Url(decodeURIComponent(location.hash.slice(PREFIX.length)));
  return new TextDecoder().decode(await pipe(packed, new DecompressionStream('deflate-raw')));
}

/** Убрать проект из адреса: перезагрузка страницы не должна снова открыть его поверх правок. */
export function clearLocationProject() {
  history.replaceState(null, '', `${location.pathname}${location.search}`);
}
