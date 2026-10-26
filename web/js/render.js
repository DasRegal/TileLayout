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
  ruler: '#5f6b7a',
  joint: '#37414d',
  jointGap: '#fbfaf6',
};

/* Ч/б печать: без заливок цветом, линии — чёрные. */
const MONO = {
  ...COLOR,
  background: '#ffffff',
  wall: '#d4d4d4',
  wallEdge: '#000000',
  grout: '#000000',
  narrow: '#ffffff',
  gap: '#ffffff',
  openingFloor: '#ffffff',
  openingEdge: '#000000',
  clip: '#000000',
  innerDim: '#000000',
  outerDim: '#000000',
  openingDim: '#000000',
  threshold: '#000000',
  ruler: '#000000',
  joint: '#000000',
  jointGap: '#ffffff',
};

/* Слои плана: что показывать (меню «Вид», настройки печати). */
export const DEFAULT_LAYERS = {
  tiles: true, numbers: false, dims: true, clips: true, labels: true, joints: true, rulers: true,
};

let C = COLOR; // палитра текущей отрисовки

const FONT = '"Segoe UI", Roboto, "Helvetica Neue", Arial, sans-serif';
const TILE_KIND = { whole: 0, cut: 1, narrow: 2 };

/**
 * Нарисовать сцену. options: {selected — id комнаты, highlight — контуры
 * выделенного предмета (плоские массивы, мм), image(reference) — картинка плитки
 * или null (ещё грузится / нет), dpr — плотность пикселей, overlay(ctx) —
 * рисование инструмента поверх, в пикселях экрана, layers — слои (DEFAULT_LAYERS),
 * numbers — номера плиток (tl_web_tile_numbers), printing — лист печати (белый
 * фон, без выделения), mono — ч/б}.
 */
export function drawScene(ctx, scene, view, options) {
  const { width, height } = ctx.canvas;
  const dpr = options.dpr;
  const layers = { ...DEFAULT_LAYERS, ...options.layers };
  const world = () => ctx.setTransform(dpr * view.scale, 0, 0, dpr * view.scale, dpr * view.panX, dpr * view.panY);
  const screen = () => ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  const px = (pixels) => pixels / view.scale; // пиксели экрана — в мм
  C = options.mono ? MONO : COLOR;
  const image = options.mono ? () => null : options.image;

  ctx.setTransform(1, 0, 0, 1, 0, 0);
  ctx.fillStyle = options.printing ? '#ffffff' : C.background;
  ctx.fillRect(0, 0, width, height);
  if (!scene) return;

  world();
  for (const space of scene.spaces) drawFloor(ctx, space, view, px, { ...options, image, layers });
  if (layers.joints) for (const joint of scene.joints) drawJoint(ctx, joint, px);
  drawWalls(ctx, scene, view, px, dpr, options.mono);
  drawOpenings(ctx, scene, view, px);
  const selected = scene.spaces.find((space) => space.id === options.selected);
  const outlines = options.printing ? [] : selected ? [selected.outline, ...selected.holes] : options.highlight || [];
  if (outlines.length) {
    ctx.beginPath();
    for (const outline of outlines) polygonPath(ctx, outline, false);
    ctx.lineWidth = px(2.5);
    ctx.strokeStyle = C.selection;
    ctx.setLineDash([px(8), px(5)]);
    ctx.stroke();
    ctx.setLineDash([]);
  }
  screen();
  if (layers.numbers && options.numbers) drawTileNumbers(ctx, scene, options.numbers, view, options.printing);
  if (layers.dims) {
    for (const dimension of scene.dimensions) drawOuterDimension(ctx, dimension, view);
    for (const opening of scene.openings) drawOpeningWidth(ctx, opening, view);
    for (const space of scene.spaces) drawRoomLengths(ctx, space, view);
  }
  if (layers.labels) for (const space of scene.spaces) drawRoomLabel(ctx, space, view);
  if (layers.rulers) for (const ruler of scene.rulers) drawRuler(ctx, ruler, view);
  if (options.overlay) options.overlay(ctx);
}

/** Номера плиток: в центре куска, узкая полоса — вдоль неё (как в программе). */
function drawTileNumbers(ctx, scene, numbers, view, printing) {
  const size = printing ? 8.7 : 10;
  ctx.font = `600 ${size}px ${FONT}`;
  ctx.textAlign = 'center';
  ctx.textBaseline = 'middle';
  for (const room of numbers) {
    const space = scene.spaces.find((s) => s.id === room.space);
    if (!space || !space.layout || !space.tile.enabled) continue;
    if (!printing && Math.min(space.tile.w, space.tile.h) * view.scale < 18) continue;
    const list = room.numbers;
    for (let i = 0; i < list.length; i += 5) {
      const [x, y] = view.toScreen(list[i], list[i + 1]);
      const text = String(list[i + 2]);
      const boxW = list[i + 3] * view.scale;
      const boxH = list[i + 4] * view.scale;
      const textW = ctx.measureText(text).width;
      let angle = room.rotation;
      if (textW + 4 > boxW && boxH > boxW) angle += 90; // узкая полоса — вдоль неё
      angle = ((((angle + 90) % 180) + 180) % 180) - 90;
      ctx.save();
      ctx.translate(x, y);
      ctx.rotate((-angle * Math.PI) / 180);
      ctx.fillStyle = printing ? '#ffffff' : 'rgba(255, 255, 255, 0.67)';
      ctx.fillRect(-textW / 2 - 2, -size / 2 - 1, textW + 4, size + 2);
      ctx.fillStyle = '#20242a';
      ctx.fillText(text, 0, 0.5);
      ctx.restore();
    }
  }
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
  if (!layout || !options.layers.tiles) {
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
    ctx.fillStyle = C.gap;
    ctx.fill('evenodd');
  }
  const [a, b, c, d, tx, ty] = layout.matrix;
  ctx.transform(a, b, c, d, tx, ty);
  polygonPath(ctx, layout.floor, true);
  for (const hole of layout.holes) polygonPath(ctx, hole, false);
  ctx.clip('evenodd');
  ctx.fillStyle = C.grout;
  ctx.fill('evenodd');
  drawTiles(ctx, space, view, px, options.image(tile.texture), options);
  ctx.restore();
}

function drawTiles(ctx, space, view, px, image, options) {
  const tile = space.tile;
  const tiles = space.layout.tiles;
  const thinJoints = options.mono || tile.gap * view.scale < 1.2; // в ч/б — контур каждой плитки
  const colors = options.mono ? ['#ffffff', '#ffffff', '#ffffff'] : [tile.color, darker(tile.color, 1.12), C.narrow];
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
      ctx.lineWidth = px(options.mono ? 0.5 : 0.6);
      ctx.strokeStyle = options.mono ? '#000000' : darker(tile.color, 1.5);
      ctx.strokeRect(left, top, w, h);
    }
  }
  if (options.layers.clips && Math.min(tile.w, tile.h) * view.scale > 24) {
    const clips = space.layout.clips;
    const size = px(5);
    ctx.fillStyle = C.clip;
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
function drawWalls(ctx, scene, view, px, dpr, mono) {
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
  ctx.strokeStyle = C.wallEdge;
  ctx.stroke();

  const pattern = mono ? null : hatch(ctx, view, dpr);
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
  ctx.fillStyle = C.wall;
  ctx.fill(rule);
  if (!pattern) return;
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
      ctx.fillStyle = C.openingFloor;
      ctx.fill();
      ctx.beginPath();
      line(ctx, [cut[0], cut[1], cut[2], cut[3]]); // линии порога
      line(ctx, [cut[6], cut[7], cut[4], cut[5]]);
      stroke(ctx, C.openingEdge, px(1), [px(4), px(4)]);
      ctx.restore();
      ctx.beginPath();
      line(ctx, [cut[2], cut[3], cut[4], cut[5]]); // откосы
      line(ctx, [cut[6], cut[7], cut[0], cut[1]]);
      stroke(ctx, C.wallEdge, px(1.4));
    } else {
      ctx.beginPath();
      for (const jamb of opening.jambs) line(ctx, jamb);
      stroke(ctx, C.wallEdge, px(1.4));
    }
    if (opening.threshold) {
      ctx.beginPath();
      line(ctx, opening.threshold);
      stroke(ctx, C.threshold, px(2), [px(5), px(5)]);
    }
  }
}

/** Ширина проёма — поперёк него, посередине. */
function drawOpeningWidth(ctx, opening, view) {
  const a = view.toScreen(...opening.from);
  const b = view.toScreen(...opening.to);
  if (Math.hypot(b[0] - a[0], b[1] - a[1]) > 22) {
    textAlong(ctx, String(Math.round(opening.width)), a, b, [0, 0], 0, C.openingDim, true);
  }
}

function stroke(ctx, color, width, dash = []) {
  ctx.strokeStyle = color;
  ctx.lineWidth = width;
  ctx.setLineDash(dash);
  ctx.stroke();
  ctx.setLineDash([]);
}

// ---- деформационные швы и линейки -------------------------------------------------

/** Шов: щель шириной gap (не уже 3 px) и пунктир по оси. */
function drawJoint(ctx, joint, px) {
  const [x0, y0, x1, y1] = joint.line;
  const length = Math.hypot(x1 - x0, y1 - y0);
  if (length < px(1)) return;
  const half = Math.max(joint.gap, px(3)) / 2;
  const [nx, ny] = [(-(y1 - y0) / length) * half, ((x1 - x0) / length) * half];
  ctx.beginPath();
  ctx.moveTo(x0 + nx, y0 + ny);
  ctx.lineTo(x1 + nx, y1 + ny);
  ctx.lineTo(x1 - nx, y1 - ny);
  ctx.lineTo(x0 - nx, y0 - ny);
  ctx.closePath();
  ctx.fillStyle = C.jointGap;
  ctx.fill();
  stroke(ctx, C.joint, px(1));
  ctx.beginPath();
  line(ctx, joint.line);
  stroke(ctx, C.joint, px(1), [px(4), px(4)]);
}

/** Линейка: отрезок с засечками и длиной (в пикселях экрана). */
export function drawRuler(ctx, ruler, view, color = C.ruler) {
  const a = view.toScreen(ruler.line[0], ruler.line[1]);
  const b = view.toScreen(ruler.line[2], ruler.line[3]);
  const length = Math.hypot(b[0] - a[0], b[1] - a[1]);
  if (length < 1) return;
  const normal = [-(b[1] - a[1]) / length, (b[0] - a[0]) / length];
  ctx.beginPath();
  ctx.moveTo(...a);
  ctx.lineTo(...b);
  for (const end of [a, b]) {
    ctx.moveTo(end[0] - normal[0] * 4, end[1] - normal[1] * 4);
    ctx.lineTo(end[0] + normal[0] * 4, end[1] + normal[1] * 4);
  }
  stroke(ctx, color, 1);
  textAlong(ctx, String(ruler.length), a, b, normal, 9, color);
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
  stroke(ctx, C.outerDim, 1);
  textAlong(ctx, String(dimension.length), da, db, [0, 0], 0, C.outerDim);
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
    textAlong(ctx, length.toFixed(0), a, b, normal, 11, C.innerDim);
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
