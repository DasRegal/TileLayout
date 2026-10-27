// Печать и PDF — листы A4 как у настольной программы (src/app/app_print.c):
// план в масштабе 1:N (если не влезает — на нескольких листах), план с
// номерами плиток, чертежи подрезок карточками 63×56 мм, лист расчёта.
// Листы собираются на странице, печатает браузер («Сохранить как PDF» — там же).

import { drawScene } from './render.js';
import { View } from './view.js';

const PX_PER_MM = 96 / 25.4; // CSS-пикселей на мм бумаги
const MARGIN_MM = 10;
const HEADER_PX = 26;
const DIMENSION_MARGIN_PX = 55; // поле под размеры вокруг плана
const CARD_WIDTH_PX = 63 * PX_PER_MM;
const CARD_HEIGHT_PX = 56 * PX_PER_MM;
const RESOLUTION = 2.5; // пикселей холста на CSS-пиксель: печать чётче экрана
const FIT_SCALES = [5, 10, 15, 20, 25, 30, 40, 50, 60, 75, 100, 125, 150, 200, 250, 300, 400, 500, 750, 1000];
export const PRINT_SCALES = [10, 20, 25, 50, 75, 100, 150, 200, 250, 500];

/**
 * Собрать листы и открыть печать браузера. options: {tiles, dims, clips, cuts,
 * calc, mono, landscape, scale (0 — по размеру листа)}; image(ссылка) — рисунок
 * плитки; loadImages(scene) — обещание: рисунки плиток сцены загружены.
 */
export async function printProject({ core, title, options, image, loadImages }) {
  const scene = core.scene();
  await loadImages(scene);
  const paper = options.landscape ? { width: 297, height: 210 } : { width: 210, height: 297 };
  const area = { width: (paper.width - 2 * MARGIN_MM) * PX_PER_MM, height: (paper.height - 2 * MARGIN_MM) * PX_PER_MM };
  const root = document.createElement('div');
  root.id = 'print-root';
  const sheets = [];
  const plan = layoutPlan(scene.bounds, area, options.scale);
  sheets.push(...planSheets(scene, plan, area, title, options, image, null));
  if (options.cuts) {
    sheets.push(...planSheets(scene, plan, area, title, options, image, core.tileNumbers()));
    sheets.push(...cardSheets(core.cutCards(), area, title, options.mono));
  }
  if (options.calc) sheets.push(calculationSheet(core.summary(), title, options.mono));
  root.append(...sheets);
  document.body.append(root);
  setPageSize(paper);
  const cleanup = () => {
    root.remove();
    window.removeEventListener('afterprint', cleanup);
  };
  window.addEventListener('afterprint', cleanup);
  window.print();
  return { pages: sheets.length, scale: plan.scale };
}

function setPageSize(paper) {
  let style = document.getElementById('print-page-style');
  if (!style) {
    style = document.createElement('style');
    style.id = 'print-page-style';
    document.head.append(style);
  }
  style.textContent = `@page { size: ${paper.width}mm ${paper.height}mm; margin: 0; }
    #print-root .sheet { width: ${paper.width}mm; height: ${paper.height}mm; }`;
}

// ---- план ---------------------------------------------------------------------

/** Масштаб и число листов: наименьший стандартный масштаб, при котором план влезает. */
function layoutPlan(bounds, area, fixedScale) {
  const [left, top, right, bottom] = bounds;
  const width = Math.max(right - left, 1);
  const height = Math.max(bottom - top, 1);
  const areaHeight = area.height - HEADER_PX;
  let scale = fixedScale;
  if (!scale) {
    const fit = Math.min((area.width - 2 * DIMENSION_MARGIN_PX) / width, (areaHeight - 2 * DIMENSION_MARGIN_PX) / height);
    const wanted = PX_PER_MM / fit;
    scale = FIT_SCALES.find((s) => s >= wanted - 1e-6) ?? Math.ceil(wanted);
  }
  const pixelsPerMm = PX_PER_MM / scale;
  const planWidth = width * pixelsPerMm;
  const planHeight = height * pixelsPerMm;
  const columns = Math.max(1, Math.ceil((planWidth + 2 * DIMENSION_MARGIN_PX) / area.width - 1e-6));
  const rows = Math.max(1, Math.ceil((planHeight + 2 * DIMENSION_MARGIN_PX) / areaHeight - 1e-6));
  return {
    scale, pixelsPerMm, columns, rows, left, top,
    originX: (columns * area.width - planWidth) / 2,
    originY: (rows * areaHeight - planHeight) / 2,
  };
}

function planSheets(scene, plan, area, title, options, image, numbers) {
  const total = plan.columns * plan.rows;
  const sheets = [];
  const areaHeight = area.height - HEADER_PX;
  for (let index = 0; index < total; index++) {
    const column = index % plan.columns;
    const row = Math.floor(index / plan.columns);
    const sheet = makeSheet(title, numbers ? 'план с номерами плиток' : 'план', `Масштаб 1:${plan.scale}    Лист ${index + 1} из ${total}`);
    const canvas = document.createElement('canvas');
    canvas.width = Math.round(area.width * RESOLUTION);
    canvas.height = Math.round(areaHeight * RESOLUTION);
    canvas.style.width = `${area.width}px`;
    canvas.style.height = `${areaHeight}px`;
    const view = new View();
    view.scale = plan.pixelsPerMm;
    view.panX = plan.originX - column * area.width - plan.left * plan.pixelsPerMm;
    view.panY = plan.originY - row * areaHeight - plan.top * plan.pixelsPerMm;
    drawScene(canvas.getContext('2d'), scene, view, {
      dpr: RESOLUTION,
      printing: true,
      mono: options.mono,
      image,
      numbers,
      layers: {
        tiles: options.tiles || Boolean(numbers), // номера без плиток бессмысленны
        numbers: Boolean(numbers),
        dims: options.dims && !numbers, // на плане с номерами — без размеров
        clips: options.clips,
        labels: !numbers, // плашки закрывали бы номера
        joints: true,
        rulers: false, // линейки — инструмент экрана
      },
    });
    sheet.querySelector('.sheet-body').append(canvas);
    sheets.push(sheet);
  }
  return sheets;
}

function makeSheet(title, subject, right) {
  const sheet = document.createElement('section');
  sheet.className = 'sheet';
  const header = document.createElement('header');
  const left = document.createElement('strong');
  left.textContent = `${title} — ${subject}`;
  const info = document.createElement('span');
  info.textContent = right;
  header.append(left, info);
  const body = document.createElement('div');
  body.className = 'sheet-body';
  sheet.append(header, body);
  return sheet;
}

// ---- чертежи подрезок ------------------------------------------------------------

function cardSheets(cards, area, title, mono) {
  if (!cards.length) return [];
  const columns = Math.max(1, Math.floor(area.width / CARD_WIDTH_PX));
  const rows = Math.max(1, Math.floor((area.height - HEADER_PX) / CARD_HEIGHT_PX));
  const perPage = columns * rows;
  const pages = Math.ceil(cards.length / perPage);
  const sheets = [];
  for (let page = 0; page < pages; page++) {
    const sheet = makeSheet(title, 'чертежи подрезок', `Чертежей: ${cards.length}    Лист ${page + 1} из ${pages}`);
    const canvas = document.createElement('canvas');
    const height = rows * CARD_HEIGHT_PX;
    const width = columns * CARD_WIDTH_PX;
    canvas.width = Math.round(width * RESOLUTION);
    canvas.height = Math.round(height * RESOLUTION);
    canvas.style.width = `${width}px`;
    canvas.style.height = `${height}px`;
    canvas.style.marginLeft = `${(area.width - width) / 2}px`;
    const ctx = canvas.getContext('2d');
    ctx.scale(RESOLUTION, RESOLUTION);
    for (let n = 0; n < perPage; n++) {
      const card = cards[page * perPage + n];
      if (!card) break;
      const x = (n % columns) * CARD_WIDTH_PX;
      const y = Math.floor(n / columns) * CARD_HEIGHT_PX;
      drawCard(ctx, card, x + 3, y + 3, CARD_WIDTH_PX - 6, CARD_HEIGHT_PX - 6, mono);
    }
    sheet.querySelector('.sheet-body').append(canvas);
    sheets.push(sheet);
  }
  return sheets;
}

/** Подписи без наложений: прямоугольник подписи — только на свободное место. */
class Labels {
  constructor(bounds) {
    this.bounds = bounds; // [left, top, right, bottom]
    this.taken = [];
  }

  fits([l, t, r, b]) {
    const [bl, bt, br, bb] = this.bounds;
    if (l < bl || t < bt || r > br || b > bb) return false;
    return !this.taken.some(([tl, tt, tr, tb]) => l < tr && r > tl && t < tb && b > tt);
  }

  /** Текст в одной из точек places (по очереди); angle — в радианах. */
  place(ctx, text, places, angle = 0, style = {}) {
    ctx.font = `${style.bold ? '600 ' : ''}${style.size || 9}px "Segoe UI", Roboto, Arial, sans-serif`;
    const width = ctx.measureText(text).width + 4;
    const height = (style.size || 9) + 3;
    const vertical = Math.abs(Math.sin(angle)) > 0.7;
    const [w, h] = vertical ? [height, width] : [width, height];
    for (const [x, y] of places) {
      const box = [x - w / 2, y - h / 2, x + w / 2, y + h / 2];
      if (!this.fits(box)) continue;
      this.taken.push(box);
      ctx.save();
      ctx.translate(x, y);
      ctx.rotate(angle);
      ctx.fillStyle = '#ffffff';
      ctx.fillRect(-width / 2, -height / 2, width, height);
      ctx.fillStyle = '#000000';
      ctx.textAlign = 'center';
      ctx.textBaseline = 'middle';
      ctx.fillText(text, 0, 0.5);
      ctx.restore();
      return true;
    }
    return false;
  }
}

function shapePath(ctx, shape, scale, ox, oy) {
  for (let i = 0; i < shape.length; i += 2) {
    const x = ox + shape[i] * scale;
    const y = oy + shape[i + 1] * scale;
    if (i === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  }
  ctx.closePath();
}

function drawCard(ctx, card, x, y, w, h, mono) {
  const tile = card.tile;
  ctx.strokeStyle = mono ? '#000000' : '#999fa6';
  ctx.lineWidth = 0.8;
  ctx.strokeRect(x, y, w, h);
  ctx.fillStyle = '#000000';
  ctx.textAlign = 'left';
  ctx.textBaseline = 'top';
  ctx.font = '600 11px "Segoe UI", Roboto, Arial, sans-serif';
  fitText(ctx, card.title, x + 5, y + 3, w - 10);
  ctx.font = '9px "Segoe UI", Roboto, Arial, sans-serif';
  fitText(ctx, card.narrow ? `${card.subtitle};  узкая (< ${tile.min_cut} мм)` : card.subtitle, x + 5, y + 17, w - 10);

  const margin = 17;
  const areaX = x + margin;
  const areaY = y + 28 + margin - 4;
  const areaW = w - 2 * margin;
  const areaH = h - 28 - 2 * margin + 2;
  const scale = Math.min(areaW / tile.w, areaH / tile.h);
  const ox = areaX + (areaW - tile.w * scale) / 2;
  const oy = areaY + (areaH - tile.h * scale) / 2;

  if (card.kind !== 'single') hatchRest(ctx, card, scale, ox, oy, mono);
  ctx.setLineDash([3, 3]);
  ctx.strokeStyle = mono ? '#000000' : '#7a8087';
  ctx.lineWidth = 0.8;
  ctx.strokeRect(ox, oy, tile.w * scale, tile.h * scale);
  ctx.setLineDash([]);

  for (const part of card.parts) {
    ctx.beginPath();
    for (const shape of part.shapes) shapePath(ctx, shape, scale, ox, oy);
    ctx.fillStyle = mono ? '#ffffff' : tile.color;
    ctx.fill('evenodd');
  }
  const labels = new Labels([x + 2, y + 26, x + w - 2, y + h - 2]);
  for (const part of card.parts) for (const shape of part.shapes) pieceLines(ctx, labels, shape, tile, scale, ox, oy, mono);
  for (const part of card.parts) for (const shape of part.shapes) pieceDimensions(ctx, labels, shape, scale, ox, oy);
  if (card.parts.length > 1) for (const part of card.parts) partLabel(ctx, labels, part, scale, ox, oy, [oy, oy + tile.h * scale]);
  if (card.kind !== 'single') restLabel(ctx, labels, card, scale, ox, oy, card.kind === 'shared' ? 'стена' : 'отход');
}

function fitText(ctx, text, x, y, width) {
  let shown = text;
  while (shown.length > 1 && ctx.measureText(shown).width > width) shown = `${shown.slice(0, -2)}…`;
  ctx.fillText(shown, x, y);
}

/** Контур куска: заводская кромка — тонко, линия реза — толсто (красным). Линии — препятствия для подписей. */
function pieceLines(ctx, labels, shape, tile, scale, ox, oy, mono) {
  const n = shape.length / 2;
  for (let i = 0; i < n; i++) {
    const [ax, ay] = [shape[2 * i], shape[2 * i + 1]];
    const [bx, by] = [shape[(2 * i + 2) % shape.length], shape[(2 * i + 3) % shape.length]];
    const horizontal = Math.abs(ay - by) < 0.5;
    const vertical = Math.abs(ax - bx) < 0.5;
    const edge = (horizontal && (Math.abs(ay) < 0.5 || Math.abs(ay - tile.h) < 0.5)) ||
      (vertical && (Math.abs(ax) < 0.5 || Math.abs(ax - tile.w) < 0.5));
    ctx.beginPath();
    ctx.moveTo(ox + ax * scale, oy + ay * scale);
    ctx.lineTo(ox + bx * scale, oy + by * scale);
    ctx.strokeStyle = edge || mono ? '#000000' : '#d9302f';
    ctx.lineWidth = edge ? 0.9 : 2.2;
    ctx.stroke();
    // линия — препятствие: узкая полоса вдоль неё
    const [sx, sy, ex, ey] = [ox + ax * scale, oy + ay * scale, ox + bx * scale, oy + by * scale];
    if (horizontal || vertical) labels.taken.push([Math.min(sx, ex) - 1, Math.min(sy, ey) - 1, Math.max(sx, ex) + 1, Math.max(sy, ey) + 1]);
  }
}

/** Размеры сторон куска: у прямоугольника — одна ширина и одна высота, снаружи стороны. */
function pieceDimensions(ctx, labels, shape, scale, ox, oy) {
  const n = shape.length / 2;
  let rectangle = n === 4;
  for (let i = 0; i < n && rectangle; i++) {
    const [ax, ay, bx, by] = [shape[2 * i], shape[2 * i + 1], shape[(2 * i + 2) % shape.length], shape[(2 * i + 3) % shape.length]];
    rectangle = Math.abs(ax - bx) < 0.5 || Math.abs(ay - by) < 0.5;
  }
  const done = { horizontal: false, vertical: false };
  const cx = shape.filter((_, i) => i % 2 === 0).reduce((a, b) => a + b, 0) / n;
  const cy = shape.filter((_, i) => i % 2 === 1).reduce((a, b) => a + b, 0) / n;
  for (let i = 0; i < n; i++) {
    const [ax, ay, bx, by] = [shape[2 * i], shape[2 * i + 1], shape[(2 * i + 2) % shape.length], shape[(2 * i + 3) % shape.length]];
    const length = Math.hypot(bx - ax, by - ay);
    if (length < 0.5) continue;
    if (rectangle) {
      const key = Math.abs(ay - by) < 0.5 ? 'horizontal' : 'vertical';
      if (done[key]) continue;
      done[key] = true;
    }
    let normal = [-(by - ay) / length, (bx - ax) / length];
    const mx = (ax + bx) / 2;
    const my = (ay + by) / 2;
    if (normal[0] * (cx - mx) + normal[1] * (cy - my) > 0) normal = [-normal[0], -normal[1]]; // наружу куска
    const sx = ox + mx * scale;
    const sy = oy + my * scale;
    let angle = Math.atan2(by - ay, bx - ax);
    if (angle > Math.PI / 2 || angle < -Math.PI / 2) angle += Math.PI;
    const places = [8, -8, 16].map((offset) => [sx + normal[0] * offset, sy + normal[1] * offset]);
    labels.place(ctx, String(Math.round(length)), places, angle);
  }
}

function partLabel(ctx, labels, part, scale, ox, oy, [tileTop, tileBottom]) {
  const shape = part.shapes[0];
  if (!shape) return;
  const xs = shape.filter((_, i) => i % 2 === 0);
  const ys = shape.filter((_, i) => i % 2 === 1);
  const cx = ox + ((Math.min(...xs) + Math.max(...xs)) / 2) * scale;
  const cy = oy + ((Math.min(...ys) + Math.max(...ys)) / 2) * scale;
  const inside = [[cx, cy], [cx, cy - 12], [cx, cy + 12], [cx, cy - 24], [cx, cy + 24]];
  const full = part.number ? `${part.room} №${part.number}` : part.room;
  const short = part.number ? `№${part.number}` : part.room;
  const style = { bold: true, size: 8 };
  if (labels.place(ctx, full, inside, 0, style) || labels.place(ctx, short, inside, 0, style)) return;
  labels.place(ctx, short, [[cx, tileTop - 10], [cx, tileBottom + 10]], 0, style);
}

/** Свободная часть плитки между двумя частями (стена или отход): штриховка и надпись. */
function restBetween(card) {
  if (card.parts.length !== 2) return null;
  const box = (part) => {
    const shape = part.shapes[0];
    const xs = shape.filter((_, i) => i % 2 === 0);
    const ys = shape.filter((_, i) => i % 2 === 1);
    return { left: Math.min(...xs), right: Math.max(...xs), top: Math.min(...ys), bottom: Math.max(...ys) };
  };
  let [a, b] = card.parts.map(box);
  if (a.left > b.left) [a, b] = [b, a];
  if (b.left - a.right > 0.5) return { left: a.right, top: 0, right: b.left, bottom: card.tile.h };
  if (a.top > b.top) [a, b] = [b, a];
  if (b.top - a.bottom > 0.5) return { left: 0, top: a.bottom, right: card.tile.w, bottom: b.top };
  return null;
}

function hatchRest(ctx, card, scale, ox, oy, mono) {
  const [w, h] = [card.tile.w * scale, card.tile.h * scale];
  ctx.save();
  ctx.beginPath();
  ctx.rect(ox, oy, w, h);
  for (const part of card.parts) for (const shape of part.shapes) shapePath(ctx, shape, scale, ox, oy);
  ctx.clip('evenodd');
  ctx.beginPath();
  for (let x = ox - h; x < ox + w; x += 4) {
    ctx.moveTo(x, oy + h);
    ctx.lineTo(x + h, oy);
  }
  ctx.strokeStyle = mono ? '#000000' : '#999fa6';
  ctx.lineWidth = 0.5;
  ctx.stroke();
  ctx.restore();
}

function restLabel(ctx, labels, card, scale, ox, oy, text) {
  const rest = restBetween(card);
  if (!rest) return;
  const [l, t, r, b] = [ox + rest.left * scale, oy + rest.top * scale, ox + rest.right * scale, oy + rest.bottom * scale];
  const tall = b - t > r - l;
  labels.place(ctx, text, [[(l + r) / 2, (t + b) / 2]], tall ? -Math.PI / 2 : 0, { size: 8 });
}

// ---- лист расчёта ------------------------------------------------------------------

const number = new Intl.NumberFormat('ru-RU', { maximumFractionDigits: 2, minimumFractionDigits: 2 });
const decimal = new Intl.NumberFormat('ru-RU', { maximumFractionDigits: 1 });

function calculationSheet(summary, title, mono) {
  const sheet = document.createElement('section');
  sheet.className = 'sheet calculation';
  const swatch = (color) => (mono ? '' : `<span class="swatch-mark" style="color:${color}">■</span> `);
  const escape = (text) => String(text).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' })[c]);
  const date = new Date().toLocaleDateString('ru-RU');
  const untiled = summary.floor_area - summary.tiled_area;
  const types = summary.types.map((type, i) => {
    const savings = [];
    if (type.shared_saving > 0) savings.push(`одна плитка на две комнаты: −${type.shared_saving}`);
    if (type.offcut_saving > 0) savings.push(`из остатков подрезок: −${type.offcut_saving}`);
    const buyArea = (type.to_buy * type.tile.w * type.tile.h) / 1e6;
    return `<tr><td>${i + 1}</td><td>${swatch(type.tile.color)}${escape(type.name)}<br><small>${type.tile.color}, шов ${type.tile.gap} мм</small></td>
      <td>${escape(type.rooms.join(', '))}${savings.length ? `<br><small>${savings.join('; ')}</small>` : ''}</td>
      <td class="n">${number.format(type.area)}</td><td class="n">${type.count}</td><td class="n">${type.tile.reserve}%</td>
      <td class="n"><b>${type.to_buy}</b></td><td class="n"><b>${number.format(buyArea)}</b></td></tr>`;
  });
  const packs = (list, unit, what) => list.map((p, i) => `${i ? ' +' : ''} <b>${p.count}</b> ${what} по ${decimal.format(p.size)} ${unit}`).join('');
  const rooms = summary.rooms.map((room) => {
    if (!room.tiled) return `<tr><td>${escape(room.name)}</td><td class="n">${number.format(room.floor_area)}</td><td class="n">—</td><td>без плитки</td><td></td><td></td><td></td></tr>`;
    const how = `${swatch(room.color)}${escape(room.tile_name)}, ${room.source ? `как в «${escape(room.source)}»` : 'своя'}${room.rotated ? ', повёрнута' : ''}`;
    const row = `<tr><td>${escape(room.name)}</td><td class="n">${number.format(room.floor_area)}</td><td class="n">${number.format(room.tiled_area)}</td>
      <td>${how}</td><td class="n">${room.count}</td><td class="n">${decimal.format(room.glue_kg)}</td><td class="n">${room.clips}</td></tr>`;
    return room.shared_tiles
      ? `${row}<tr><td></td><td colspan="6"><small>из них ${room.shared_tiles} плиток общие с соседней комнатой — одна плитка на обе</small></td></tr>`
      : row;
  });
  sheet.innerHTML = `
    <h1>${escape(title)}</h1>
    <p>Дата: ${date}<br>Площадь комнат: <b>${number.format(summary.room_area)} м²</b>; под плитку: <b>${number.format(summary.tiled_area)} м²</b> (с полом под дверьми)${untiled > 0.001 ? `, без плитки: ${number.format(untiled)} м²` : ''}</p>
    <h2>Плитка к покупке</h2>
    <table><thead><tr><th>№</th><th>Плитка</th><th>Комнаты</th><th class="n">Площадь, м²</th><th class="n">Нужно, шт.</th><th class="n">Запас</th><th class="n">Купить, шт.</th><th class="n">Купить, м²</th></tr></thead>
    <tbody>${types.join('')}</tbody></table>
    <h2>Клей и СВП</h2>
    <p>${summary.glue.total_kg > 0 ? `Клей: <b>${decimal.format(summary.glue.total_kg)} кг</b> =${packs(summary.glue.packs, 'кг', 'мешк.')}<br>` : ''}
       ${summary.clips.total > 0 ? `СВП: <b>${summary.clips.total} шт.</b> =${packs(summary.clips.packs, 'шт.', 'уп.')}` : ''}</p>
    <h2>По комнатам</h2>
    <table><thead><tr><th>Комната</th><th class="n">Площадь, м²</th><th class="n">Под плитку, м²</th><th>Плитка / раскладка</th><th class="n">Плиток</th><th class="n">Клей, кг</th><th class="n">СВП</th></tr></thead>
    <tbody>${rooms.join('')}</tbody></table>
    <p><small>«Нужно» — плиток на укладку с учётом общих плиток на стыке комнат с общей раскладкой и использования остатков
    подрезок. В таблице по комнатам — плитки каждой комнаты отдельно.</small></p>`;
  return sheet;
}
