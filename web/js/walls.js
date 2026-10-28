// Развёртка стен комнаты на Canvas — как в настольной программе (src/app/app_render_walls.c):
// стены в ряд, как видно из комнаты; плитка полос, проёмы, под стенами — пол у них, верх облицовки,
// отверстия и линейки на стенах, номера, размеры, выделенное.
// Данные — от ядра (src/web/tl_web_walls.c), всё в мм: x — вдоль стен, y — вниз от потолка.

import { drawLayout, drawPanno, drawRuler } from './render.js';
import { drawPipe } from './tools.js';

const COLOR = {
  background: '#f4f5f7',
  face: '#fbfaf6',
  edge: '#2c3038',
  floor: '#6f7681',
  opening: '#ffffff',
  openingEdge: '#7a6a4f',
  dim: '#1f4e9c',
  finish: '#8a5a00',
  floorStrip: '#f0ede6',
  selection: '#2563eb',
};

const FONT = '"Segoe UI", Roboto, "Helvetica Neue", Arial, sans-serif';
const LABEL_SPACE = 500; // мм над стенами и под полосой пола — подписи и размеры

/** Габарит развёртки с подписями — для «вписать». */
export function wallsBounds(data) {
  const [left, top, right, bottom] = data.bounds;
  const extra = (data.stairs || []).length ? LABEL_SPACE : 0; // подпись лестницы шире её столбца
  return [left - LABEL_SPACE, top - LABEL_SPACE, right + extra, bottom + LABEL_SPACE];
}

/**
 * Нарисовать развёртку. options: {dpr, image(reference), layers (слои плана), printing,
 * marks: {wall, pipe, ruler} — выделенное (номера), preview: [a, b] — новая линейка в развёртке}.
 */
export function drawRoomWalls(ctx, data, view, options) {
  const { width, height } = ctx.canvas;
  const dpr = options.dpr;
  const world = () => ctx.setTransform(dpr * view.scale, 0, 0, dpr * view.scale, dpr * view.panX, dpr * view.panY);
  const screen = () => ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  const px = (pixels) => pixels / view.scale;
  ctx.setTransform(1, 0, 0, 1, 0, 0);
  ctx.fillStyle = options.printing ? '#ffffff' : COLOR.background;
  ctx.fillRect(0, 0, width, height);
  if (!data) return;
  const H = data.height;

  world();
  for (const wall of data.walls) {
    ctx.fillStyle = COLOR.face;
    ctx.fillRect(wall.x, 0, wall.length, H);
  }
  drawStairs(ctx, data, px, false);
  if (options.layers.tiles) {
    for (const layout of data.layouts) {
      if (!layout.too_many && !layout.reveal) drawLayout(ctx, layout.tile, layout, view, px, { ...options, mono: false });
    }
  }
  if (options.layers.tiles) for (const panno of data.pannos) drawPanno(ctx, panno, view, px, options);
  for (const hole of data.holes) drawHole(ctx, hole, px);
  if (options.layers.tiles) { // откосы — внутри проёмов, поверх них
    for (const layout of data.layouts) if (layout.reveal && !layout.too_many) drawLayout(ctx, layout.tile, layout, view, px, options);
  }
  drawFloorStrips(ctx, data, view, px, options);
  if (data.enabled && data.finish_top < H - 0.5) { // верх облицовки
    ctx.beginPath();
    for (const wall of data.walls) {
      ctx.moveTo(wall.x, H - data.finish_top);
      ctx.lineTo(wall.x + wall.length, H - data.finish_top);
    }
    ctx.setLineDash([px(6), px(6)]);
    ctx.strokeStyle = COLOR.finish;
    ctx.lineWidth = px(1.2);
    ctx.stroke();
    ctx.setLineDash([]);
  }
  for (const wall of data.walls) { // контур стены и пол
    ctx.strokeStyle = COLOR.edge;
    ctx.lineWidth = px(1.2);
    ctx.strokeRect(wall.x, 0, wall.length, H);
    ctx.beginPath();
    ctx.moveTo(wall.x - 60, H);
    ctx.lineTo(wall.x + wall.length + 60, H);
    ctx.strokeStyle = COLOR.floor;
    ctx.lineWidth = px(3);
    ctx.stroke();
  }

  drawStairs(ctx, data, px, true);

  const marks = options.marks || {};
  const chosenPanno = data.pannos.find((panno) => panno.index === marks.panno);
  if (chosenPanno) {
    ctx.beginPath();
    const points = chosenPanno.outline;
    for (let i = 0; i < points.length; i += 2) (i ? ctx.lineTo : ctx.moveTo).call(ctx, points[i], points[i + 1]);
    ctx.closePath();
    ctx.setLineDash([px(7), px(5)]);
    ctx.strokeStyle = COLOR.selection;
    ctx.lineWidth = px(2.5);
    ctx.stroke();
    ctx.setLineDash([]);
  }
  const chosen = data.walls[marks.wall ?? -1];
  if (chosen) {
    ctx.setLineDash([px(7), px(5)]);
    ctx.strokeStyle = COLOR.selection;
    ctx.lineWidth = px(2.5);
    ctx.strokeRect(chosen.x, 0, chosen.length, H);
    ctx.setLineDash([]);
  }

  screen();
  if (options.layers.tiles && options.layers.numbers) drawNumbers(ctx, data, view, options.printing);
  data.pipes.forEach((pipe, i) => {
    if (pipe.wall !== null) {
      drawPipe(ctx, view.toScreen(...pipe.point), (pipe.diameter * view.scale) / 2, i === marks.pipe ? COLOR.selection : undefined);
    }
  });
  data.rulers.forEach((ruler, i) => {
    if (ruler.wall !== null) {
      drawRuler(ctx, { line: [...ruler.a, ...ruler.b], length: ruler.length }, view, i === marks.ruler ? COLOR.selection : undefined);
    }
  });
  if (options.preview) {
    const [a, b] = options.preview;
    drawRuler(ctx, { line: [...a, ...b], length: Math.round(Math.hypot(b[0] - a[0], b[1] - a[1])) }, view, COLOR.selection);
  }
  drawHoleLabels(ctx, data, view);
  for (const stairs of data.stairs || []) { // подпись над лестницей
    const [left, top, right] = stairs.rect;
    const [x, y] = view.toScreen((left + right) / 2, top);
    ctx.font = `bold 10px ${FONT}`;
    ctx.fillStyle = '#5d6470';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'bottom';
    ctx.fillText('Лестница', x, y - 6);
  }
  if (options.layers.dims) drawLabels(ctx, data, view);
}

/** Лестницы — столбцами справа от стен: подступенки и проступи снизу вверх; outline — контуры и пол. */
function drawStairs(ctx, data, px, outline) {
  for (const stairs of data.stairs || []) {
    const [left, , right] = stairs.rect;
    let y = data.height;
    for (let step = 0; step < stairs.steps; step++) {
      for (const [size, tread] of [[stairs.riser, false], [stairs.tread, true]]) {
        if (outline) {
          ctx.strokeStyle = tread ? COLOR.edge : '#8a919c';
          ctx.lineWidth = px(tread ? 1.2 : 0.8);
          ctx.strokeRect(left, y - size, right - left, size);
        } else {
          ctx.fillStyle = tread ? '#e6e1d8' : COLOR.face;
          ctx.fillRect(left, y - size, right - left, size);
        }
        y -= size;
      }
    }
    if (outline) {
      ctx.beginPath();
      ctx.moveTo(left - 60, data.height);
      ctx.lineTo(right + 60, data.height);
      ctx.strokeStyle = COLOR.floor;
      ctx.lineWidth = px(3);
      ctx.stroke();
    }
  }
}

/** Под стеной — пол у неё, как будто откинут вниз: плитка пола и зон; к низу — светлее. */
function drawFloorStrips(ctx, data, view, px, options) {
  const H = data.height;
  data.walls.forEach((wall, i) => {
    ctx.save();
    ctx.beginPath();
    ctx.rect(wall.x, H, wall.length, data.floor_depth);
    ctx.fillStyle = COLOR.floorStrip;
    ctx.fill();
    ctx.clip();
    if (options.layers.tiles) {
      for (const layout of data.floors[i] || []) {
        if (layout.panno) drawPanno(ctx, layout, view, px, options); // панно на полу
        else if (!layout.too_many) drawLayout(ctx, layout.tile, layout, view, px, { ...options, layers: { ...options.layers, clips: false } });
      }
    }
    const fade = ctx.createLinearGradient(0, H, 0, H + data.floor_depth);
    fade.addColorStop(0, 'rgba(255, 255, 255, 0)');
    fade.addColorStop(1, `rgba(255, 255, 255, ${options.printing ? 0.6 : 0.75})`);
    ctx.fillStyle = fade;
    ctx.fillRect(wall.x, H, wall.length, data.floor_depth);
    ctx.restore();
  });
}

function drawHole(ctx, hole, px) {
  const [left, top, right, bottom] = hole.rect;
  if (hole.kind === 'fixture') { // предмет закрывает стену — вид спереди
    ctx.fillStyle = '#eceef1';
    ctx.fillRect(left, top, right - left, bottom - top);
    ctx.strokeStyle = '#5d6470';
    ctx.lineWidth = px(1.4);
    ctx.strokeRect(left, top, right - left, bottom - top);
    return;
  }
  ctx.fillStyle = COLOR.opening;
  ctx.fillRect(left, top, right - left, bottom - top);
  ctx.strokeStyle = COLOR.openingEdge;
  ctx.lineWidth = px(1.4);
  ctx.strokeRect(left, top, right - left, bottom - top);
  ctx.beginPath();
  const middle = (left + right) / 2;
  if (hole.kind === 'door') { // полотно: от низа углов к середине верха
    ctx.moveTo(left, bottom);
    ctx.lineTo(middle, top);
    ctx.lineTo(right, bottom);
    ctx.setLineDash([px(5), px(5)]);
  } else if (hole.kind === 'window') { // рама — крест
    ctx.moveTo(middle, top);
    ctx.lineTo(middle, bottom);
    ctx.moveTo(left, (top + bottom) / 2);
    ctx.lineTo(right, (top + bottom) / 2);
  }
  ctx.lineWidth = px(0.8);
  ctx.stroke();
  ctx.setLineDash([]);
}

function drawHoleLabels(ctx, data, view) {
  ctx.font = `600 10px ${FONT}`;
  ctx.textAlign = 'center';
  ctx.textBaseline = 'middle';
  ctx.fillStyle = COLOR.openingEdge;
  for (const hole of data.holes) {
    const [left, top, right, bottom] = hole.rect;
    const text = `${hole.name ?? ''} ${Math.round(right - left)}×${Math.round(bottom - top)}`.trim();
    if (ctx.measureText(text).width > (right - left) * view.scale + 40) continue;
    const [x, y] = view.toScreen((left + right) / 2, top + (bottom - top) / 4);
    ctx.fillText(text, x, y);
  }
}

function drawNumbers(ctx, data, view, printing) {
  const size = printing ? 8.7 : 10;
  ctx.font = `600 ${size}px ${FONT}`;
  ctx.textAlign = 'center';
  ctx.textBaseline = 'middle';
  for (const layout of data.layouts) {
    if (Math.min(layout.tile.w, layout.tile.h) * view.scale < (printing ? 10 : 18)) continue; // фриз мельче номера
    const list = layout.numbers;
    for (let i = 0; i < list.length; i += 3) {
      const [x, y] = view.toScreen(list[i], list[i + 1]);
      const text = String(list[i + 2]);
      const textW = ctx.measureText(text).width;
      ctx.fillStyle = printing ? '#ffffff' : 'rgba(255, 255, 255, 0.67)';
      ctx.fillRect(x - textW / 2 - 2, y - size / 2 - 1, textW + 4, size + 2);
      ctx.fillStyle = '#20242a';
      ctx.fillText(text, x, y + 0.5);
    }
  }
}

/** Над стеной — «Стена N», под полом — длина; слева от первой — высота помещения. */
function drawLabels(ctx, data, view) {
  ctx.textAlign = 'center';
  ctx.textBaseline = 'middle';
  data.walls.forEach((wall, i) => {
    const [x, top] = view.toScreen(wall.x + wall.length / 2, 0);
    ctx.font = `600 12px ${FONT}`;
    ctx.fillStyle = COLOR.edge;
    ctx.fillText(`Стена ${i + 1}`, x, top - 14);
    const [, floor] = view.toScreen(0, data.height + data.floor_depth);
    ctx.font = `11.3px ${FONT}`;
    ctx.fillStyle = COLOR.dim;
    ctx.fillText(String(Math.round(wall.length)), x, floor + 12);
  });
  if (data.walls.length) {
    const [x, y] = view.toScreen(0, data.height / 2);
    ctx.save();
    ctx.translate(x - 16, y);
    ctx.rotate(-Math.PI / 2);
    ctx.font = `11.3px ${FONT}`;
    ctx.fillStyle = COLOR.dim;
    ctx.fillText(String(Math.round(data.height)), 0, 0);
    ctx.restore();
  }
  if (data.enabled && data.finish_top < data.height - 0.5 && data.walls.length) {
    const [x, y] = view.toScreen(data.walls[0].x, data.height - data.finish_top);
    ctx.font = `10px ${FONT}`;
    ctx.textAlign = 'left';
    ctx.fillStyle = COLOR.finish;
    ctx.fillText(`облицовка до ${Math.round(data.finish_top)}`, x + 6, y - 9);
  }
}
