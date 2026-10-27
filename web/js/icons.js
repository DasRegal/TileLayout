// Значки интерфейса: линии 24×24, цвет — currentColor (как текст кнопки).
// Кнопке с data-icon="имя" значок вставляется перед текстом (installIcons).

const ICONS = {
  select: '<path d="M6 3.5 18 13l-5.2.9 2.8 5.6-2.3 1.1-2.8-5.6L6 18.5z"/>',
  rect: '<rect x="4" y="6" width="16" height="12" rx="0.5"/>',
  contour: '<path d="M4.5 17.5 6 6.5l7.5-2.5 6 6-3 8.5z"/><circle cx="4.5" cy="17.5" r="1.3"/><circle cx="6" cy="6.5" r="1.3"/><circle cx="13.5" cy="4" r="1.3"/><circle cx="19.5" cy="10" r="1.3"/><circle cx="16.5" cy="18.5" r="1.3"/>',
  partition: '<path d="M3 20h18M3 4h18"/><rect x="10" y="4" width="4" height="16" fill="currentColor" stroke="none" opacity=".35"/><path d="M10 4v16M14 4v16"/>',
  opening: '<path d="M2.5 20h5.5M16 20h5.5M8 20v-8M8 12a8 8 0 0 1 8 8"/>',
  joint: '<path d="M9 3v18M15 3v18"/><path d="M12 3v18" stroke-dasharray="2 2"/>',
  pipe: '<rect x="3.5" y="3.5" width="17" height="17" rx="1"/><circle cx="12" cy="12" r="4.5"/><path d="M9.5 12h5M12 9.5v5"/>',
  ruler: '<path d="M3.5 15.5 15.5 3.5l5 5-12 12z"/><path d="M7 12l2 2M9.5 9.5l1.5 1.5M12 7l2 2M14.5 4.5 16 6"/>',
  open: '<path d="M3 7.5V18a1.5 1.5 0 0 0 1.5 1.5h15A1.5 1.5 0 0 0 21 18V9.5A1.5 1.5 0 0 0 19.5 8h-8l-2-2.5h-5A1.5 1.5 0 0 0 3 7z"/>',
  save: '<path d="M12 4v11M7.5 10.5 12 15l4.5-4.5M4.5 18.5h15"/>',
  demo: '<path d="M4 11 12 4l8 7v8.5a.5.5 0 0 1-.5.5H14v-6h-4v6H4.5a.5.5 0 0 1-.5-.5z"/>',
  undo: '<path d="M9 7 5 11l4 4"/><path d="M5 11h9.5a4.5 4.5 0 0 1 0 9H12"/>',
  redo: '<path d="m15 7 4 4-4 4"/><path d="M19 11H9.5a4.5 4.5 0 0 0 0 9H12"/>',
  fit: '<path d="M4 9V4h5M15 4h5v5M20 15v5h-5M9 20H4v-5"/><rect x="8" y="8" width="8" height="8" rx="1"/>',
  house: '<path d="M4 11 12 4l8 7"/><path d="M6 9.5V20h12V9.5"/><path d="M9 20v-5h6v5"/>',
  view: '<path d="M2.5 12S6 5.5 12 5.5 21.5 12 21.5 12 18 18.5 12 18.5 2.5 12 2.5 12z"/><circle cx="12" cy="12" r="3"/>',
  image: '<rect x="3.5" y="5" width="17" height="14" rx="1.5"/><circle cx="9" cy="10" r="1.6"/><path d="m4 17 5-4.5 3.5 3 3-2.5L20 17"/>',
  print: '<path d="M7 9V4h10v5"/><rect x="3.5" y="9" width="17" height="7.5" rx="1.5"/><path d="M7 14h10v6H7z"/>',
  plus: '<path d="M12 5v14M5 12h14"/>',
  minus: '<path d="M5 12h14"/>',
  catalog: '<rect x="4" y="4" width="7" height="7" rx="1"/><rect x="13" y="4" width="7" height="7" rx="1"/><rect x="4" y="13" width="7" height="7" rx="1"/><rect x="13" y="13" width="7" height="7" rx="1"/>',
  heart: '<path d="M12 20s-7.5-4.6-7.5-10A4.3 4.3 0 0 1 12 7.2 4.3 4.3 0 0 1 19.5 10c0 5.4-7.5 10-7.5 10z"/>',
  rooms: '<rect x="3.5" y="4" width="8" height="7" rx="1"/><rect x="12.5" y="4" width="8" height="7" rx="1"/><rect x="3.5" y="13" width="17" height="7" rx="1"/>',
  trash: '<path d="M4.5 7h15M10 7V4.5h4V7M6.5 7l1 13h9l1-13"/>',
};

export function icon(name) {
  const body = ICONS[name];
  if (!body) return '';
  return `<svg class="icon" viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${body}</svg>`;
}

/** Значки — в кнопки с data-icon (перед текстом). */
export function installIcons(root = document) {
  for (const element of root.querySelectorAll('[data-icon]')) {
    if (element.querySelector(':scope > svg.icon')) continue;
    element.insertAdjacentHTML('afterbegin', icon(element.dataset.icon));
  }
}
