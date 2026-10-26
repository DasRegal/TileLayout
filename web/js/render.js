// Рисование плана на Canvas — как в настольной программе (src/app/app_render.c):
// пол с раскладкой, стены и перегородки одним силуэтом, проёмы, размеры,
// подписи комнат. Сцена — от ядра (src/web/tl_web_scene.c), всё в мм.

const COLOR = {
  background: '#f4f5f7',
  wall: '#6f7681',
  wallEdge: '#2c3038',
  grout: '#8f8a82',
  narrow: '#e8826e',
  gap: '#f3eee4',
  openingFloor: '#ece5d5',
  openingEdge: '#7a6a4f',
  clip: '#c2410c',
  innerDim: '#1f4e9c',
  outerDim: '#8a5a00',
  openingDim: '#6b3fa0',
  threshold: '#a0522d',
  selection: '#2563eb',
};

const FONT = '"Segoe UI", Roboto, "Helvetica Neue", Arial, sans-serif';
const TILE_KIND = { whole: 0, cut: 1, narrow: 2 };

/**
 * Нарисовать сцену. options: {selected — id комнаты, image(reference) — картинка
 * плитки или null (ещё грузится / нет), dpr — плотность пикселей}.
 */
export function drawScene(ctx, scene, view, options) {
  const { width, height } = ctx.canvas;
  const dpr = options.dpr;
  const world = () => ctx.setTransform(dpr * view.scale, 0, 0, dpr * view.scale, dpr * view.panX, dpr * view.panY);
  const screen = () => ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  const px = (pixels) => pixels / view.scale; // пиксели экрана — в мм

  ctx.setTransform(1, 0, 0, 1, 0, 0);
  ctx.fillStyle = COLOR.background;
  ctx.fillRect(0, 0, width, height);
  if (!scene) return;

  world();
  for (const space of scene.spaces) drawFloor(ctx, space, view, px, options);
  drawWalls(ctx, scene, view, px, dpr);
  drawOpenings(ctx, scene, view, px);
  const selected = scene.spaces.find((space) => space.id === options.selected);
  if (selected) {
    polygonPath(ctx, selected.outline, true);
    for (const hole of selected.holes) polygonPath(ctx, hole, false);
    ctx.lineWidth = px(2.5);
    ctx.strokeStyle = COLOR.selection;
    ctx.setLineDash([px(8), px(5)]);
    ctx.stroke();
    ctx.setLineDash([]);
  }
  screen();
  for (const dimension of scene.dimensions) drawOuterDimension(ctx, dimension, view);
  for (const opening of scene.openings) drawOpeningWidth(ctx, opening, view);
  for (const space of scene.spaces) drawRoomLengths(ctx, space, view);
  for (const space of scene.spaces) drawRoomLabel(ctx, space, view);
}

// ---- контуры -----------------------------------------------------------------

function polygonPath(ctx, points, begin) {
  if (begin) ctx.beginPath();
  for (let i = 0; i < points.length; i += 2) {
    if (i === 0) ctx.moveTo(points[0], points[1]);
    else ctx.lineTo(points[i], points[i + 1]);
  }
  if (points.length) ctx.closePath();
}

function line(ctx, segment) {
  ctx.moveTo(segment[0], segment[1]);
  ctx.lineTo(segment[2], segment[3]);
}

/** Вся видимая область в мм — рамка для «рисовать всюду, кроме». */
function visibleRect(ctx, view) {
  const [left, top] = view.toWorld(-10, -10);
  const [right, bottom] = view.toWorld(ctx.canvas.width + 10, ctx.canvas.height + 10);
  return [left, top, right - left, bottom - top];
}

/** Обрезать всё, кроме многоугольников (пол под проёмами). */
function clipOut(ctx, view, polygons) {
  ctx.beginPath();
  ctx.rect(...visibleRect(ctx, view));
  for (const polygon of polygons) polygonPath(ctx, polygon, false);
  ctx.clip('evenodd');
}

// ---- пол и плитка ----------------------------------------------------------------

function hexToRgb(hex) {
  const value = parseInt(hex.slice(1), 16);
  return [(value >> 16) & 255, (value >> 8) & 255, value & 255];
}

function darker(hex, factor) {
  const [r, g, b] = hexToRgb(hex).map((c) => Math.round(c / factor));
  return `rgb(${r}, ${g}, ${b})`;
}

function drawFloor(ctx, space, view, px, options) {
  const tile = space.tile;
  ctx.save();
  polygonPath(ctx, space.outline, true);
  for (const hole of space.holes) polygonPath(ctx, hole, false);
  const layout = space.layout;
  if (!layout) {
    ctx.fillStyle = '#ffffff';
    ctx.fill('evenodd');
    ctx.restore();
    return;
  }
  ctx.clip('evenodd');
  if (layout.too_many) {
    ctx.fillStyle = tile.color;
    ctx.fill('evenodd');
    ctx.restore();
    return;
  }
  if (tile.wall_gap > 0) {
    ctx.fillStyle = COLOR.gap;
    ctx.fill('evenodd');
  }
  const [a, b, c, d, tx, ty] = layout.matrix;
  ctx.transform(a, b, c, d, tx, ty);
  polygonPath(ctx, layout.floor, true);
  for (const hole of layout.holes) polygonPath(ctx, hole, false);
  ctx.clip('evenodd');
  ctx.fillStyle = COLOR.grout;
  ctx.fill('evenodd');
  drawTiles(ctx, space, view, px, options.image(tile.texture));
  ctx.restore();
}

function drawTiles(ctx, space, view, px, image) {
  const tile = space.tile;
  const tiles = space.layout.tiles;
  const thinJoints = tile.gap * view.scale < 1.2;
  const colors = [tile.color, darker(tile.color, 1.12), COLOR.narrow];
  for (let i = 0; i < tiles.length; i += 5) {
    const left = tiles[i];
    const top = tiles[i + 1];
    const w = tiles[i + 2] - left;
    const h = tiles[i + 3] - top;
    const kind = tiles[i + 4];
    if (image) {
      drawTexture(ctx, image, left, top, w, h);
      if (kind !== TILE_KIND.whole) {
        ctx.fillStyle = kind === TILE_KIND.narrow ? 'rgba(232, 130, 110, 0.7)' : 'rgba(0, 0, 0, 0.12)';
        ctx.fillRect(left, top, w, h);
      }
    } else {
      ctx.fillStyle = colors[kind];
      ctx.fillRect(left, top, w, h);
    }
    if (thinJoints) {
      ctx.lineWidth = px(0.6);
      ctx.strokeStyle = darker(tile.color, 1.5);
      ctx.strokeRect(left, top, w, h);
    }
  }
  if (Math.min(tile.w, tile.h) * view.scale > 24) {
    const clips = space.layout.clips;
    const size = px(5);
    ctx.fillStyle = COLOR.clip;
    for (let i = 0; i < clips.length; i += 2) ctx.fillRect(clips[i] - size / 2, clips[i + 1] - size / 2, size, size);
  }
}

/** Рисунок — на прямоугольник плитки. Картинка лежит вдоль длинной стороны
 *  плитки: плитка поперёк картинки — рисунок повёрнут на 90°. */
function drawTexture(ctx, image, left, top, w, h) {
  const iw = image.naturalWidth;
  const ih = image.naturalHeight;
  ctx.save();
  if (iw >= ih === w >= h || Math.abs(w - h) < 0.5) {
    ctx.drawImage(image, left, top, w, h);
  } else {
    ctx.transform(0, h / iw, -w / ih, 0, left + w, top);
    ctx.drawImage(image, 0, 0);
  }
  ctx.restore();
}

// ---- стены и перегородки ---------------------------------------------------------

let hatchCache = null;

/** Штриховка тел стен: 8 px экрана при любом масштабе. */
function hatch(ctx, view, dpr) {
  const size = Math.round(8 * dpr);
  if (!hatchCache || hatchCache.size !== size) {
    const tile = document.createElement('canvas');
    tile.width = tile.height = size;
    const t = tile.getContext('2d');
    t.strokeStyle = 'rgba(255, 255, 255, 0.16)';
    t.lineWidth = dpr;
    t.beginPath();
    t.moveTo(0, size);
    t.lineTo(size, 0);
    t.stroke();
    hatchCache = { size, tile };
  }
  const pattern = ctx.createPattern(hatchCache.tile, 'repeat');
  pattern.setTransform(new DOMMatrix().scale(1 / (view.scale * dpr)));
  return pattern;
}

/** Сначала все обводки вдвое толще, затем заливки поверх — стыки скрываются. */
function drawWalls(ctx, scene, view, px, dpr) {
  ctx.save();
  clipOut(ctx, view, scene.doorways);
  ctx.beginPath();
  for (const wall of scene.walls) {
    polygonPath(ctx, wall.outer, false);
    polygonPath(ctx, wall.inner, false);
  }
  for (const body of scene.partitions) polygonPath(ctx, body, false);
  ctx.lineJoin = 'miter';
  ctx.miterLimit = 1.5;
  ctx.lineWidth = px(2.8);
  ctx.strokeStyle = COLOR.wallEdge;
  ctx.stroke();

  const pattern = hatch(ctx, view, dpr);
  ctx.beginPath();
  for (const wall of scene.walls) {
    polygonPath(ctx, wall.outer, false);
    polygonPath(ctx, wall.inner, false);
  }
  fillWithHatch(ctx, pattern, 'evenodd');
  ctx.beginPath();
  for (const body of scene.partitions) polygonPath(ctx, body, false);
  fillWithHatch(ctx, pattern, 'nonzero');
  ctx.restore();
}

function fillWithHatch(ctx, pattern, rule) {
  ctx.fillStyle = COLOR.wall;
  ctx.fill(rule);
  ctx.fillStyle = pattern;
  ctx.fill(rule);
}

// ---- проёмы -------------------------------------------------------------------

function drawOpenings(ctx, scene, view, px) {
  ctx.lineJoin = 'round';
  for (const opening of scene.openings) {
    if (opening.kind === 'wall') {
      const cut = opening.cut;
      ctx.save();
      if (opening.threshold) clipOut(ctx, view, scene.doorways); // пол под проёмом не закрашиваем
      polygonPath(ctx, cut, true);
      ctx.fillStyle = COLOR.openingFloor;
      ctx.fill();
      ctx.beginPath();
      line(ctx, [cut[0], cut[1], cut[2], cut[3]]); // линии порога
      line(ctx, [cut[6], cut[7], cut[4], cut[5]]);
      stroke(ctx, COLOR.openingEdge, px(1), [px(4), px(4)]);
      ctx.restore();
      ctx.beginPath();
      line(ctx, [cut[2], cut[3], cut[4], cut[5]]); // откосы
      line(ctx, [cut[6], cut[7], cut[0], cut[1]]);
      stroke(ctx, COLOR.wallEdge, px(1.4));
    } else {
      ctx.beginPath();
      for (const jamb of opening.jambs) line(ctx, jamb);
      stroke(ctx, COLOR.wallEdge, px(1.4));
    }
    if (opening.threshold) {
      ctx.beginPath();
      line(ctx, opening.threshold);
      stroke(ctx, COLOR.threshold, px(2), [px(5), px(5)]);
    }
  }
}

/** Ширина проёма — поперёк него, посередине. */
function drawOpeningWidth(ctx, opening, view) {
  const a = view.toScreen(...opening.from);
  const b = view.toScreen(...opening.to);
  if (Math.hypot(b[0] - a[0], b[1] - a[1]) > 22) {
    textAlong(ctx, String(Math.round(opening.width)), a, b, [0, 0], 0, COLOR.openingDim, true);
  }
}

function stroke(ctx, color, width, dash = []) {
  ctx.strokeStyle = color;
  ctx.lineWidth = width;
  ctx.setLineDash(dash);
  ctx.stroke();
  ctx.setLineDash([]);
}

// ---- подписи (в пикселях экрана) ---------------------------------------------------

/** Текст вдоль отрезка a→b (px), не вверх ногами; offset — сдвиг по normal. */
function textAlong(ctx, text, a, b, normal, offset, color, bold = false) {
  let angle = Math.atan2(b[1] - a[1], b[0] - a[0]);
  if (angle > Math.PI / 2 || angle < -Math.PI / 2) angle += Math.PI;
  const x = (a[0] + b[0]) / 2 + normal[0] * offset;
  const y = (a[1] + b[1]) / 2 + normal[1] * offset;
  ctx.save();
  ctx.translate(x, y);
  ctx.rotate(angle);
  ctx.font = `${bold ? '600 ' : ''}11.3px ${FONT}`;
  ctx.textAlign = 'center';
  ctx.textBaseline = 'middle';
  const width = ctx.measureText(text).width;
  ctx.fillStyle = 'rgba(255, 255, 255, 0.84)';
  roundRect(ctx, -width / 2 - 3, -8, width + 6, 16, 3);
  ctx.fill();
  ctx.fillStyle = color;
  ctx.fillText(text, 0, 0.5);
  ctx.restore();
}

function roundRect(ctx, x, y, w, h, r) {
  ctx.beginPath();
  ctx.moveTo(x + r, y);
  ctx.arcTo(x + w, y, x + w, y + h, r);
  ctx.arcTo(x + w, y + h, x, y + h, r);
  ctx.arcTo(x, y + h, x, y, r);
  ctx.arcTo(x, y, x + w, y, r);
  ctx.closePath();
}

/** Размер стены снаружи дома: размерная линия с засечками в 18 px от стены. */
function drawOuterDimension(ctx, dimension, view) {
  const a = view.toScreen(...dimension.from);
  const b = view.toScreen(...dimension.to);
  if (Math.hypot(b[0] - a[0], b[1] - a[1]) < 28) return;
  const [nx, ny] = dimension.normal;
  const offset = 18;
  const da = [a[0] + nx * offset, a[1] + ny * offset];
  const db = [b[0] + nx * offset, b[1] + ny * offset];
  ctx.beginPath();
  ctx.moveTo(...da);
  ctx.lineTo(...db);
  for (const end of [a, b]) {
    ctx.moveTo(...end);
    ctx.lineTo(end[0] + nx * (offset + 4), end[1] + ny * (offset + 4));
  }
  const length = Math.hypot(db[0] - da[0], db[1] - da[1]);
  const along = [(db[0] - da[0]) / length, (db[1] - da[1]) / length];
  const tick = [(along[0] + along[1]) * 3, (along[1] - along[0]) * 3]; // засечка 45°
  for (const end of [da, db]) {
    ctx.moveTo(end[0] - tick[0], end[1] - tick[1]);
    ctx.lineTo(end[0] + tick[0], end[1] + tick[1]);
  }
  stroke(ctx, COLOR.outerDim, 1);
  textAlong(ctx, String(dimension.length), da, db, [0, 0], 0, COLOR.outerDim);
}

/** Точка внутри многоугольника (плоский массив). */
function inside(points, x, y) {
  let result = false;
  for (let i = 0, j = points.length - 2; i < points.length; j = i, i += 2) {
    const [xi, yi, xj, yj] = [points[i], points[i + 1], points[j], points[j + 1]];
    if (yi > y !== yj > y && x < ((xj - xi) * (y - yi)) / (yj - yi) + xi) result = !result;
  }
  return result;
}

/** Длины стен комнаты — внутри, вдоль каждой стороны. */
function drawRoomLengths(ctx, space, view) {
  const room = space.room;
  const n = room.length / 2;
  for (let i = 0; i < n; i++) {
    const [x0, y0] = [room[2 * i], room[2 * i + 1]];
    const [x1, y1] = [room[(2 * i + 2) % room.length], room[(2 * i + 3) % room.length]];
    const a = view.toScreen(x0, y0);
    const b = view.toScreen(x1, y1);
    if (Math.hypot(b[0] - a[0], b[1] - a[1]) < 28) continue;
    const length = Math.hypot(x1 - x0, y1 - y0);
    let normal = [-(y1 - y0) / length, (x1 - x0) / length];
    const probe = 1 / view.scale; // 1 px внутрь
    if (!inside(room, (x0 + x1) / 2 + normal[0] * probe, (y0 + y1) / 2 + normal[1] * probe)) {
      normal = [-normal[0], -normal[1]];
    }
    textAlong(ctx, length.toFixed(0), a, b, normal, 11, COLOR.innerDim);
  }
}

/** Подпись комнаты: название, площадь, плиток, общий пол. */
function drawRoomLabel(ctx, space, view) {
  const lines = [space.name, `${space.area.toFixed(2)} м²`];
  if (space.layout && !space.layout.too_many) lines.push(`плиток: ${space.layout.count}`);
  if (space.source) lines.push(`пол как: ${space.source}`);
  const [x, y] = view.toScreen(...space.label);
  const size = 12.7;
  const lineHeight = size * 1.3;
  ctx.font = `${size}px ${FONT}`;
  const widths = lines.map((text, i) => {
    ctx.font = `${i === 0 ? '600 ' : ''}${size}px ${FONT}`;
    return ctx.measureText(text).width;
  });
  const w = Math.max(...widths) + 16;
  const h = lines.length * lineHeight + 8;
  roundRect(ctx, x - w / 2, y - h / 2, w, h, 5);
  ctx.fillStyle = 'rgba(255, 255, 255, 0.9)';
  ctx.fill();
  ctx.strokeStyle = 'rgba(44, 48, 56, 0.35)';
  ctx.lineWidth = 1;
  ctx.stroke();
  ctx.fillStyle = '#1f2328';
  ctx.textAlign = 'center';
  ctx.textBaseline = 'middle';
  lines.forEach((text, i) => {
    ctx.font = `${i === 0 ? '600 ' : ''}${size}px ${FONT}`;
    ctx.fillText(text, x, y - h / 2 + 4 + lineHeight * (i + 0.5));
  });
}
