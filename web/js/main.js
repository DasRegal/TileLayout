// TileLayout в браузере: связывает ядро, план, инструменты, панель, каталоги и файлы.

import { Catalogs, chooseTile } from './catalogs.js';
import { loadCore } from './core.js';
import { Panel, showSummary } from './panel.js';
import { PRINT_SCALES, printProject } from './print.js';
import { installIcons } from './icons.js';
import { DEFAULT_LAYERS, drawScene } from './render.js';
import { acceptDroppedFiles, downloadFile, pickFile, readDraft, readSettings, writeDraft, writeSettings } from './storage.js';
import { TOOL_HINTS, Tools } from './tools.js';
import { attachViewControls, View } from './view.js';

const PICK_PX = 6; // допуск выбора предмета, пиксели экрана
const SNAP_PX = 10; // допуск притягивания при перетаскивании
const DRAGGABLE = new Set(['vertex', 'wall', 'partition', 'node', 'tee', 'opening', 'threshold', 'ruler', 'joint', 'pipe']);
const CURSORS = {
  vertex: 'crosshair', node: 'move', tee: 'move', wall: 'move', partition: 'move', opening: 'ew-resize',
  threshold: 'move', ruler: 'move', joint: 'move',
};

const state = {
  core: null,
  scene: null,
  selection: null, // предмет плана (tl_web_pick): {kind, index, part, end, space}; у комнат ещё more — id
  multi: false, // «Несколько комнат»: касание добавляет комнату к выделенным (как Shift+щелчок)
  object: null, // свойства выделенного предмета (tl_web_object)
  guides: [], // линии выравнивания при перетаскивании
  fileName: 'plan.tilelayout.json',
  settings: {
    houseThickness: 200,
    partitionThickness: 100,
    openingWidth: 800,
    grid: 10,
    jointGap: 10,
    pipeDiameter: 50,
    keepAngles: true,
    house: { width: 6000, height: 4000 },
    print: { tiles: true, dims: true, clips: true, cuts: true, calc: true, mono: false, landscape: true, scale: 0 },
    ...readSettings(),
  },
  numbers: null, // номера плиток (tl_web_tile_numbers), когда слой включён
};

state.settings.layers = { ...DEFAULT_LAYERS, ...state.settings.layers };

const canvas = document.getElementById('plan');
const ctx = canvas.getContext('2d');
const view = new View();
const status = document.getElementById('status');
let frame = 0;

const selectedSpace = () => (state.selection?.kind === 'space' ? state.selection.space : 0);
/** Все выделенные комнаты (Shift+щелчок — несколько): первая — выделенная раньше всех. */
const selectedSpaces = () => (selectedSpace() ? [state.selection.space, ...(state.selection.more || [])] : []);

const catalogs = new Catalogs(() => requestDraw());
const tools = new Tools({
  get core() {
    return state.core;
  },
  view,
  settings: state.settings,
  edited: (selection) => {
    state.selection = selection;
    changed();
  },
  done: () => {
    const selection = state.selection;
    setTool('select');
    state.selection = selection; // нарисованная комната остаётся выделенной
    refresh();
  },
  say,
});
const panel = new Panel(document.getElementById('panel'), {
  setField: (key, value) => editRoom(() => state.core.setRoomsTileFields(selectedSpaces(), { [key]: value })),
  chooseTile: () => pickTile(),
  colorOnly: () => editRoom(() => state.core.setRoomsTileFields(selectedSpaces(), { texture: '' })),
  toggleMulti: () => {
    state.multi = !state.multi;
    say(state.multi ? 'Касайтесь комнат, чтобы добавить или убрать их; поля применятся ко всем.' : '');
    refresh();
  },
  rename: (name) => editRoom(() => state.core.setSpaceName(selectedSpace(), name)),
  align: (mode) => {
    const result = state.core.alignTiles(selectedSpace(), mode);
    say(result.message);
    if (result.ok) changed();
  },
  setSetting: (key, value) => {
    state.settings[key] = value;
    writeSettings(state.settings);
    requestDraw();
  },
  setObjectField: (key, value) => {
    const selection = state.selection;
    const { kind, index, part } = selection;
    const object = state.object;
    const core = state.core;
    const point = (x, y) => {
      const result = core.setPoint(selection, x, y);
      if (result) state.selection = result;
      return Boolean(result);
    };
    const setters = {
      'partition.thickness': () => core.setPartitionThickness(index, value),
      'partition.length': () => core.setPartitionLength(index, value),
      'partition.angle': () => core.setPartitionAngle(index, value),
      'wall.thickness': () => core.setWallThickness(index, part, value),
      'wall.length': () => core.setWallLength(index, part, value, false, state.settings.keepAngles),
      'wall.outer_length': () => core.setWallLength(index, part, value, true, state.settings.keepAngles),
      'opening.width': () => core.setOpeningWidth(index, value),
      'opening.from_start': () => core.placeOpening(index, value, false),
      'opening.to_end': () => core.placeOpening(index, value, true),
      'threshold.right': () => core.setThreshold(index, value, object.skew),
      'threshold.skew': () => core.setThreshold(index, object.right, value),
      'joint.gap': () => core.setJointGap(index, value),
      'ruler.name': () => core.setName(selection, value),
      'opening.name': () => core.setName(selection, value),
      'pipe.diameter': () => core.setPipe(index, object.x, object.y, value),
      'pipe.x': () => core.setPipe(index, value, object.y, object.diameter),
      'pipe.y': () => core.setPipe(index, object.x, value, object.diameter),
      x: () => point(value, object.y),
      y: () => point(object.x, value),
      locked: () => core.setLocked(selection, value),
    };
    if (key === 'keep_angles') {
      state.settings.keepAngles = value;
      writeSettings(state.settings);
      refresh();
      return;
    }
    const set = setters[`${kind}.${key}`] || ((key === 'x' || key === 'y' || key === 'locked') && setters[key]);
    if (set && set()) changed();
    else say(key === 'locked' ? 'Не удалось.' : 'Значение не подходит или предмет заблокирован.');
  },
  setFloorSource: (id) => editRoom(() => state.core.setSpaceSource(selectedSpace(), id)),
  swapTile: () => editRoom(() => state.core.swapTiles(selectedSpaces())),
  deleteObject: () => deleteSelection(),
});

// ---- рисование ----------------------------------------------------------------

function requestDraw() {
  if (!frame) frame = requestAnimationFrame(draw);
}

function draw() {
  frame = 0;
  const dpr = window.devicePixelRatio || 1;
  const box = canvas.getBoundingClientRect();
  const width = Math.round(box.width * dpr);
  const height = Math.round(box.height * dpr);
  if (canvas.width !== width || canvas.height !== height) {
    canvas.width = width;
    canvas.height = height;
  }
  drawScene(ctx, state.scene, view, {
    selected: selectedSpaces(),
    highlight: highlightOf(state.object),
    image: (ref) => catalogs.image(ref),
    dpr,
    layers: state.settings.layers,
    numbers: state.numbers,
    overlay: (c) => {
      drawHandles(c);
      tools.draw(c);
    },
  });
}

/** Контуры выделенного предмета для подсветки. */
function highlightOf(object) {
  return object ? object.outline : [];
}

const HANDLE_PX = 10; // маркер выделенного предмета ловится в стольких пикселях

const sameItem = (a, b) => a && b && a.kind === b.kind && a.index === b.index && (a.part ?? 0) === (b.part ?? 0) && (a.end ?? 0) === (b.end ?? 0);

/** Маркеры выделенного (концы, углы, середина, узлы) и линии выравнивания — в пикселях экрана. */
function drawHandles(c) {
  const object = state.object;
  if (object) {
    for (const handle of object.handles) {
      const [sx, sy] = view.toScreen(...handle.point);
      const active = sameItem(handle.grab, state.selection) && handle.shape !== 'middle';
      c.fillStyle = active ? '#2563eb' : '#ffffff';
      c.strokeStyle = '#2563eb';
      c.lineWidth = 1.5;
      c.beginPath();
      if (handle.shape === 'corner') { // ромб: угол тела или узла
        c.moveTo(sx, sy - 5);
        c.lineTo(sx + 5, sy);
        c.lineTo(sx, sy + 5);
        c.lineTo(sx - 5, sy);
        c.closePath();
      } else if (handle.shape === 'middle' || handle.shape === 'node') { // круг: тянуть целиком
        c.arc(sx, sy, handle.shape === 'middle' ? 6 : 5, 0, 2 * Math.PI);
      } else {
        c.rect(sx - 4, sy - 4, 8, 8);
      }
      c.fill();
      c.stroke();
      if (handle.shape === 'middle') { // стрелки «тянуть»
        c.beginPath();
        c.arc(sx, sy, 2, 0, 2 * Math.PI);
        c.fillStyle = '#2563eb';
        c.fill();
      }
    }
  }
  const far = 100000; // линия — через весь экран
  for (const [x, y, vertical] of state.guides) {
    const [sx, sy] = view.toScreen(x, y);
    c.beginPath();
    if (vertical) {
      c.moveTo(sx, -far);
      c.lineTo(sx, far);
    } else {
      c.moveTo(-far, sy);
      c.lineTo(far, sy);
    }
    c.setLineDash([4, 4]);
    c.strokeStyle = '#1a73e8';
    c.lineWidth = 1;
    c.stroke();
    c.setLineDash([]);
  }
}

/** Масштаб кнопками — у середины плана. */
function zoomBy(factor) {
  const box = canvas.getBoundingClientRect();
  view.zoomAt(box.width / 2, box.height / 2, factor);
  requestDraw();
}

function fitView() {
  const box = canvas.getBoundingClientRect();
  if (state.scene) view.fit(state.scene.bounds, box.width, box.height);
  requestDraw();
}

// ---- состояние ------------------------------------------------------------------

let summaryTimer = 0;

/** После любого изменения: сцена, выделение, панель, расчёт, кнопки. */
function refresh() {
  const core = state.core;
  state.scene = core.scene();
  const selection = state.selection;
  const exists = (id) => state.scene.spaces.some((space) => space.id === id);
  if (selection?.kind === 'space' && !exists(selection.space)) {
    state.selection = null;
  } else if (selection?.more) {
    selection.more = selection.more.filter(exists);
  }
  state.object = state.selection && state.selection.kind !== 'space' ? core.object(state.selection) : null;
  if (state.object?.kind === 'wall') state.object.keep_angles = state.settings.keepAngles;
  state.numbers = state.settings.layers.numbers ? core.tileNumbers() : null;
  if (state.selection && state.selection.kind !== 'space' && !state.object) state.selection = null;
  panel.show(selectedSpaces().map((id) => core.spaceTile(id)), catalogs, state.multi);
  panel.showObject(state.object);
  panel.showTool(tools.tool, state.settings, TOOL_HINTS[tools.tool]);
  document.getElementById('sheet-title').textContent = sheetTitle();
  document.querySelector('[data-action="undo"]').disabled = !core.canUndo();
  document.querySelector('[data-action="redo"]').disabled = !core.canRedo();
  clearTimeout(summaryTimer); // расчёт — чуть позже: при вводе подряд не пересчитывать на каждое поле
  summaryTimer = setTimeout(() => showSummary(document.getElementById('summary'), core.summary(), catalogs), 150);
  requestDraw();
}

/** Заголовок шторки настроек на телефоне: что выделено. */
function sheetTitle() {
  const rooms = selectedSpaces();
  if (rooms.length > 1) return `Комнат: ${rooms.length} — плитка, клей, СВП`;
  if (rooms.length === 1) return `${state.scene.spaces.find((s) => s.id === rooms[0])?.name ?? 'Комната'} — плитка`;
  if (state.object) return panel.objectTitle(state.object);
  return tools.tool === 'select' ? 'Расчёт к покупке' : 'Настройки инструмента';
}

function toggleSheet(open = !document.body.classList.contains('sheet-open')) {
  document.body.classList.toggle('sheet-open', open);
  document.getElementById('sheet-toggle').setAttribute('aria-expanded', String(open));
}

/** Проект изменился: обновить всё и сохранить черновик. */
function changed() {
  refresh();
  if (!writeDraft(state.core.save(), state.fileName)) {
    say('Черновик в браузере не сохранился — сохраните проект файлом.');
  }
}

function editRoom(change) {
  if (!selectedSpace()) return;
  if (!change()) {
    say('Значение не подходит.');
    return;
  }
  changed();
}

function say(text) {
  status.textContent = text;
}

function deleteSelection() {
  const selection = state.selection;
  if (!selection) return;
  if (selection.kind === 'space') {
    say('Комнату образуют стены: удалите перегородку или весь дом (щёлкните его стену).');
    return;
  }
  if (selection.kind === 'tee' || selection.kind === 'threshold') {
    say(selection.kind === 'tee' ? 'Примыкание удаляется вместе с перегородкой: выделите её саму.' : 'Стык удаляется вместе с проёмом.');
    return;
  }
  if (!state.core.deleteObject(selection)) {
    say(selection.kind === 'vertex' ? 'У дома должно остаться хотя бы три угла.' : 'Не удалось удалить: заблокировано в настольной программе.');
    return;
  }
  state.selection = null;
  changed();
  say('Удалено. Отменить — Ctrl+Z.');
}

function setTool(tool) {
  tools.set(tool);
  document.body.classList.toggle('drawing', tool !== 'select');
  for (const button of document.querySelectorAll('[data-tool]')) {
    button.setAttribute('aria-pressed', String(button.dataset.tool === tool));
  }
  if (tool !== 'select') {
    state.selection = null;
    state.multi = false;
  }
  refresh();
}

function openProject({ name, text }) {
  try {
    state.core.open(text);
  } catch (error) {
    say(`Не удалось открыть «${name}»: ${error.message}`);
    return;
  }
  state.fileName = name;
  state.selection = null;
  changed();
  fitView();
  say(`Открыт «${name}». Щёлкните комнату, чтобы настроить плитку.`);
}

async function pickTile() {
  if (!selectedSpace()) return;
  const room = state.core.spaceTile(selectedSpace());
  const choice = await chooseTile(catalogs, room.tile.texture);
  if (choice === 'color-only') {
    editRoom(() => state.core.setRoomsTileFields(selectedSpaces(), { texture: '' }));
  } else if (choice) {
    const fields = await catalogs.fieldsFor(choice.catalog, choice.tile);
    editRoom(() => state.core.setRoomsTileFields(selectedSpaces(), fields));
  }
}

/** Комнату — к выделенным или из них (Shift+щелчок, «Несколько комнат»). */
function toggleSpace(id) {
  const rooms = selectedSpaces();
  const next = rooms.includes(id) ? rooms.filter((room) => room !== id) : [...rooms, id];
  state.selection = next.length ? { kind: 'space', space: next[0], more: next.slice(1) } : null;
  const count = next.length;
  say(count > 1 ? `Выбрано комнат: ${count}. Поля плитки, клея и СВП применяются ко всем.` : '');
}

const adding = (event) => state.multi || Boolean(event?.shiftKey);

function select(world, event) {
  const handle = handleAt(world);
  const hit = handle ? handle.grab : state.core.pick(world[0], world[1], PICK_PX / view.scale);
  if (hit.kind === 'space' && adding(event)) {
    toggleSpace(hit.space);
    refresh();
    return;
  }
  state.selection = hit.kind === 'none' ? null : hit;
  refresh();
  say('');
}

/** Нажатие в «Выборе»: предмет под курсором тянется; выделенная комната — дом
 *  целиком, с Ctrl — сетка плитки; иначе комната и пустое место — сдвиг вида. */
function startDrag(world, event) {
  if (!state.core || tools.drawing) return null;
  const handle = handleAt(world);
  const hit = handle ? handle.grab : state.core.pick(world[0], world[1], PICK_PX / view.scale);
  let target = DRAGGABLE.has(hit.kind) ? hit : null;
  if (hit.kind === 'space') {
    if (adding(event)) return null; // щелчок добавит комнату к выделенным
    if (event.ctrlKey || event.metaKey) target = { kind: 'tiles' };
    else if (hit.space === selectedSpace()) target = { kind: 'house' };
  }
  if (!target || !state.core.dragBegin(target, world[0], world[1])) return null;
  state.selection = hit;
  refresh();
  return {
    move(point, event) {
      const result = state.core.dragMove(point[0], point[1], SNAP_PX / view.scale, state.settings.grid, event.shiftKey);
      state.selection = result.selection;
      state.guides = result.guides;
      refresh();
    },
    end(moved) {
      const result = state.core.dragEnd();
      state.guides = [];
      if (!moved) return;
      state.selection = result.kind === 'none' ? null : result;
      changed();
    },
  };
}

/** Маркер выделенного предмета под точкой (ближайший) или null. */
function handleAt(world) {
  if (!state.object) return null;
  const [x, y] = view.toScreen(...world);
  let best = null;
  let bestDistance = HANDLE_PX;
  for (const handle of state.object.handles) {
    const [hx, hy] = view.toScreen(...handle.point);
    const distance = Math.hypot(hx - x, hy - y);
    if (distance < bestDistance) {
      best = handle;
      bestDistance = distance;
    }
  }
  return best;
}

function updateCursor(world) {
  if (tools.drawing) {
    canvas.style.cursor = '';
    return;
  }
  const handle = handleAt(world);
  if (handle) {
    canvas.style.cursor = handle.shape === 'middle' || handle.shape === 'node' ? 'move' : 'crosshair';
    return;
  }
  const hit = state.core.pick(world[0], world[1], PICK_PX / view.scale);
  canvas.style.cursor = hit.kind === 'space' && hit.space === selectedSpace() ? 'move' : CURSORS[hit.kind] || '';
}

// ---- команды -------------------------------------------------------------------

const commands = {
  demo() {
    state.core.newDemo();
    state.fileName = 'plan.tilelayout.json';
    state.selection = null;
    changed();
    fitView();
    say('Пример проекта. Щёлкните комнату, чтобы настроить плитку.');
  },
  async open() {
    const file = await pickFile();
    if (file) openProject(file);
  },
  save() {
    downloadFile(state.core.save(), state.fileName);
    say(`Сохранено: ${state.fileName} — в папке загрузок браузера.`);
  },
  undo() {
    if (state.core.undo()) changed();
  },
  redo() {
    if (state.core.redo()) changed();
  },
  fit: fitView,
  'zoom-in': () => zoomBy(1.25),
  'zoom-out': () => zoomBy(1 / 1.25),
  catalog: pickTile,
  house: addHouse,
  png: exportPng,
  print: openPrintDialog,
  thanks: () => document.getElementById('thanks-dialog').showModal(),
};

// ---- вид, PNG, печать ------------------------------------------------------------

function bindLayers() {
  for (const input of document.querySelectorAll('[data-layer]')) {
    input.checked = state.settings.layers[input.dataset.layer];
    input.addEventListener('change', () => {
      state.settings.layers[input.dataset.layer] = input.checked;
      writeSettings(state.settings);
      if (state.core) refresh();
    });
  }
}

const baseName = () => state.fileName.replace(/\.tilelayout\.json$|\.json$/i, '') || 'plan';

/** Весь план картинкой: слои — как на экране, длинная сторона — 3000 px. */
async function exportPng() {
  const scene = state.core.scene();
  await catalogs.loadImages(scene);
  const [left, top, right, bottom] = scene.bounds;
  const longest = 3000;
  const margin = 80;
  const scale = (longest - 2 * margin) / Math.max(right - left, bottom - top, 1);
  const image = document.createElement('canvas');
  image.width = Math.round((right - left) * scale + 2 * margin);
  image.height = Math.round((bottom - top) * scale + 2 * margin);
  const pngView = new View();
  pngView.scale = scale;
  pngView.panX = margin - left * scale;
  pngView.panY = margin - top * scale;
  drawScene(image.getContext('2d'), scene, pngView, {
    dpr: 1,
    printing: true,
    image: (ref) => catalogs.image(ref),
    layers: state.settings.layers,
    numbers: state.settings.layers.numbers ? state.core.tileNumbers() : null,
  });
  image.toBlob((blob) => {
    const url = URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.href = url;
    link.download = `${baseName()}.png`;
    document.body.append(link);
    link.click();
    link.remove();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
    say(`Картинка сохранена: ${baseName()}.png — в папке загрузок браузера.`);
  }, 'image/png');
}

function openPrintDialog() {
  const dialog = document.getElementById('print-dialog');
  const options = state.settings.print;
  const checks = ['tiles', 'dims', 'clips', 'cuts', 'calc', 'mono'];
  for (const key of checks) dialog.querySelector(`#print-${key}`).checked = options[key];
  dialog.querySelector('#print-orientation').value = options.landscape ? 'landscape' : 'portrait';
  const scale = dialog.querySelector('#print-scale');
  scale.replaceChildren(new Option('По размеру листа', '0'), ...PRINT_SCALES.map((n) => new Option(`1:${n}`, String(n))));
  scale.value = String(options.scale);
  dialog.onclose = async () => {
    if (dialog.returnValue !== 'ok') return;
    for (const key of checks) options[key] = dialog.querySelector(`#print-${key}`).checked;
    options.landscape = dialog.querySelector('#print-orientation').value === 'landscape';
    options.scale = Number(scale.value);
    writeSettings(state.settings);
    say('Готовлю листы…');
    const result = await printProject({
      core: state.core,
      title: baseName(),
      options,
      image: (ref) => catalogs.image(ref),
      loadImages: (scene) => catalogs.loadImages(scene),
    });
    say(`Листов: ${result.pages}, план 1:${result.scale}. PDF — «Сохранить как PDF» в окне печати.`);
  };
  dialog.returnValue = '';
  dialog.showModal();
}

/** «Дом по размерам…»: окно с внутренними размерами и толщиной стен. */
function addHouse() {
  const dialog = document.getElementById('house-dialog');
  const inputs = {
    width: dialog.querySelector('#house-width'),
    height: dialog.querySelector('#house-height'),
    thickness: dialog.querySelector('#house-thickness'),
  };
  inputs.width.value = state.settings.house.width;
  inputs.height.value = state.settings.house.height;
  inputs.thickness.value = state.settings.houseThickness;
  dialog.onclose = () => {
    if (dialog.returnValue !== 'ok') return;
    const [width, height, thickness] = [inputs.width, inputs.height, inputs.thickness].map((input) => Number(input.value));
    const space = state.core.addHouse(width, height, thickness);
    if (!space) {
      say('Размеры не подходят: ширина и длина — от 100 мм.');
      return;
    }
    state.settings.house = { width, height };
    state.settings.houseThickness = thickness;
    writeSettings(state.settings);
    state.selection = { kind: 'space', space };
    changed();
    fitView();
    say('Дом добавлен.');
  };
  dialog.returnValue = '';
  dialog.showModal();
}

const TOOL_KEYS = { v: 'select', r: 'rect', c: 'contour', p: 'partition', o: 'opening', j: 'joint', h: 'pipe', l: 'ruler' };

function onKey(event) {
  const typing = event.target.matches('input, select, textarea');
  if (document.querySelector('dialog[open]')) return;
  if (event.ctrlKey || event.metaKey) {
    const key = event.key.toLowerCase();
    const command = { z: event.shiftKey ? 'redo' : 'undo', y: 'redo', o: 'open', s: 'save', k: 'catalog', p: 'print' }[key];
    if (!command || (typing && (key === 'z' || key === 'y'))) return;
    event.preventDefault();
    commands[command]();
    return;
  }
  if (typing) return;
  if (tools.key(event)) {
    event.preventDefault();
    requestDraw();
    return;
  }
  const tool = TOOL_KEYS[event.key.toLowerCase()];
  if (tool) {
    setTool(tool);
  } else if (event.key === 'Escape') {
    if (tools.drawing) setTool('select');
    else {
      state.selection = null;
      state.multi = false;
      refresh();
    }
  } else if ((event.key === 'Delete' || event.key === 'Backspace') && !tools.drawing) {
    event.preventDefault();
    deleteSelection();
  }
}

function bindCommands() {
  installIcons();
  document.getElementById('sheet-toggle').addEventListener('click', () => toggleSheet());
  bindLayers();
  for (const button of document.querySelectorAll('[data-action]')) {
    const command = commands[button.dataset.action];
    if (command) button.addEventListener('click', () => command());
  }
  for (const button of document.querySelectorAll('[data-tool]')) {
    button.addEventListener('click', () => setTool(button.dataset.tool));
  }
  document.addEventListener('keydown', onKey);
  attachViewControls(canvas, view, {
    onChange: requestDraw,
    onClick: (world, event) => {
      if (!state.core) return;
      if (tools.drawing) {
        tools.click(world, event.shiftKey);
        tools.hover(world, event.shiftKey);
        requestDraw();
      } else {
        select(world, event);
      }
    },
    onHover: (world, event) => {
      if (!state.core) return;
      updateCursor(world);
      if (!tools.drawing) return;
      tools.hover(world, event.shiftKey);
      requestDraw();
    },
    onDragStart: (world, event) => startDrag(world, event),
  });
  canvas.addEventListener('dblclick', (event) => {
    if (tools.drawing) {
      tools.doubleClick();
      requestDraw();
      return;
    }
    const box = canvas.getBoundingClientRect();
    const [x, y] = view.toWorld(event.clientX - box.left, event.clientY - box.top);
    const hit = state.core.pick(x, y, PICK_PX / view.scale);
    if (hit.kind === 'wall' && state.core.insertVertex(hit.index, hit.part, x, y, state.settings.grid)) {
      state.selection = { kind: 'vertex', index: hit.index, part: hit.part + 1, end: 0 };
      changed();
      say('Новый угол на стене — тяните его.');
    }
  });
  acceptDroppedFiles(document.body, openProject);
  // план поменял размер (шторка на телефоне, окно): середина вида остаётся на месте
  let size = null;
  new ResizeObserver(() => {
    const box = canvas.getBoundingClientRect();
    if (size && box.width > 0 && box.height > 0) {
      view.panX += (box.width - size.width) / 2;
      view.panY += (box.height - size.height) / 2;
    }
    if (box.width > 0 && box.height > 0) size = { width: box.width, height: box.height };
    requestDraw();
  }).observe(canvas);
}

// ---- запуск ---------------------------------------------------------------------

async function start() {
  bindCommands();
  try {
    [state.core] = await Promise.all([loadCore(), catalogs.load()]);
  } catch (error) {
    say(`Не удалось запустить: ${error.message}. Обновите страницу; нужен современный браузер.`);
    return;
  }
  document.body.classList.remove('loading');
  document.getElementById('version').textContent = `версия ${state.core.version()}`;
  const draft = readDraft();
  let message = 'Пример проекта. Щёлкните комнату, чтобы настроить плитку; откройте свой файл кнопкой «Открыть».';
  if (draft) {
    try {
      state.core.open(draft.text);
      state.fileName = draft.name;
      message = 'Продолжаем последний проект. Новый пример — кнопка «Пример».';
    } catch {
      state.core.newDemo();
    }
  } else {
    state.core.newDemo();
  }
  refresh();
  fitView();
  say(message);
}

start();
