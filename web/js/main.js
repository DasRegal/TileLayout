// TileLayout в браузере: связывает ядро, план, инструменты, панель, каталоги и файлы.

import { Catalogs, chooseTile } from './catalogs.js';
import { loadCore } from './core.js';
import { Panel, showSummary } from './panel.js';
import { drawScene } from './render.js';
import { acceptDroppedFiles, downloadFile, pickFile, readDraft, readSettings, writeDraft, writeSettings } from './storage.js';
import { TOOL_HINTS, Tools } from './tools.js';
import { attachViewControls, View } from './view.js';

const PICK_PX = 6; // допуск выбора предмета, пиксели экрана
const SNAP_PX = 10; // допуск притягивания при перетаскивании
const DRAGGABLE = new Set(['vertex', 'wall', 'partition', 'node', 'tee', 'opening', 'threshold', 'ruler', 'joint']);
const CURSORS = {
  vertex: 'crosshair', node: 'move', tee: 'move', wall: 'move', partition: 'move', opening: 'ew-resize',
  threshold: 'move', ruler: 'move', joint: 'move',
};

const state = {
  core: null,
  scene: null,
  selection: null, // предмет плана (tl_web_pick): {kind, index, part, end, space}
  object: null, // свойства выделенного предмета (tl_web_object)
  guides: [], // линии выравнивания при перетаскивании
  fileName: 'plan.tilelayout.json',
  settings: { houseThickness: 200, partitionThickness: 100, openingWidth: 800, grid: 10, jointGap: 10, ...readSettings() },
};

const canvas = document.getElementById('plan');
const ctx = canvas.getContext('2d');
const view = new View();
const status = document.getElementById('status');
let frame = 0;

const selectedSpace = () => (state.selection?.kind === 'space' ? state.selection.space : 0);

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
  setField: (key, value) => editRoom(() => state.core.setTileField(selectedSpace(), key, value)),
  chooseTile: () => pickTile(),
  colorOnly: () => editRoom(() => state.core.setTileField(selectedSpace(), 'texture', '')),
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
    const { kind, index, part } = state.selection;
    const core = state.core;
    const setters = {
      'partition.thickness': () => core.setPartitionThickness(index, value),
      'partition.length': () => core.setPartitionLength(index, value),
      'partition.angle': () => core.setPartitionAngle(index, value),
      'wall.thickness': () => core.setWallThickness(index, part, value),
      'opening.width': () => core.setOpeningWidth(index, value),
      'joint.gap': () => core.setJointGap(index, value),
    };
    const set = setters[`${kind}.${key}`];
    if (set && set()) changed();
    else say('Значение не подходит.');
  },
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
    selected: selectedSpace(),
    highlight: highlightOf(state.object),
    image: (ref) => catalogs.image(ref),
    dpr,
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
  if (selection?.kind === 'space' && !state.scene.spaces.some((space) => space.id === selection.space)) {
    state.selection = null;
  }
  state.object = state.selection && state.selection.kind !== 'space' ? core.object(state.selection) : null;
  if (state.selection && state.selection.kind !== 'space' && !state.object) state.selection = null;
  panel.show(selectedSpace() ? core.spaceTile(selectedSpace()) : null, catalogs);
  panel.showObject(state.object);
  panel.showTool(tools.tool, state.settings, TOOL_HINTS[tools.tool]);
  document.querySelector('[data-action="undo"]').disabled = !core.canUndo();
  document.querySelector('[data-action="redo"]').disabled = !core.canRedo();
  clearTimeout(summaryTimer); // расчёт — чуть позже: при вводе подряд не пересчитывать на каждое поле
  summaryTimer = setTimeout(() => showSummary(document.getElementById('summary'), core.summary(), catalogs), 150);
  requestDraw();
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
  if (tool !== 'select') state.selection = null;
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
    editRoom(() => state.core.setTileField(selectedSpace(), 'texture', ''));
  } else if (choice) {
    const fields = await catalogs.fieldsFor(choice.catalog, choice.tile);
    editRoom(() => state.core.setTileFields(selectedSpace(), fields));
  }
}

function select(world) {
  const handle = handleAt(world);
  const hit = handle ? handle.grab : state.core.pick(world[0], world[1], PICK_PX / view.scale);
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
  catalog: pickTile,
};

const TOOL_KEYS = { v: 'select', r: 'rect', c: 'contour', p: 'partition', o: 'opening', j: 'joint', l: 'ruler' };

function onKey(event) {
  const typing = event.target.matches('input, select, textarea');
  if (document.querySelector('dialog[open]')) return;
  if (event.ctrlKey || event.metaKey) {
    const key = event.key.toLowerCase();
    const command = { z: event.shiftKey ? 'redo' : 'undo', y: 'redo', o: 'open', s: 'save', k: 'catalog' }[key];
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
      refresh();
    }
  } else if ((event.key === 'Delete' || event.key === 'Backspace') && !tools.drawing) {
    event.preventDefault();
    deleteSelection();
  }
}

function bindCommands() {
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
        select(world);
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
  canvas.addEventListener('dblclick', () => {
    tools.doubleClick();
    requestDraw();
  });
  acceptDroppedFiles(document.body, openProject);
  new ResizeObserver(requestDraw).observe(canvas);
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
