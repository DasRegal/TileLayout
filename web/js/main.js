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
const DRAGGABLE = new Set(['vertex', 'wall', 'partition', 'node', 'tee', 'opening']);
const CURSORS = { vertex: 'crosshair', node: 'move', tee: 'move', wall: 'move', partition: 'move', opening: 'ew-resize' };

const state = {
  core: null,
  scene: null,
  selection: null, // предмет плана (tl_web_pick): {kind, index, part, end, space}
  object: null, // свойства выделенного предмета (tl_web_object)
  guides: [], // линии выравнивания при перетаскивании
  fileName: 'plan.tilelayout.json',
  settings: { houseThickness: 200, partitionThickness: 100, openingWidth: 800, grid: 10, ...readSettings() },
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
  setObjectSize: (value) => {
    const { kind, index, part } = state.selection;
    const ok =
      kind === 'partition'
        ? state.core.setPartitionThickness(index, value)
        : kind === 'wall'
          ? state.core.setWallThickness(index, part, value)
          : state.core.setOpeningWidth(index, value);
    if (ok) changed();
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

/** Маркеры выделенного (углы, концы, узлы) и линии выравнивания — в пикселях экрана. */
function drawHandles(c) {
  const object = state.object;
  if (object) {
    object.handles.forEach(([x, y], i) => {
      const [sx, sy] = view.toScreen(x, y);
      c.fillStyle = object.kind === 'vertex' && i === object.part ? '#2563eb' : '#ffffff'; // взятый угол — залит
      c.strokeStyle = '#2563eb';
      c.lineWidth = 1.5;
      c.fillRect(sx - 4, sy - 4, 8, 8);
      c.strokeRect(sx - 4, sy - 4, 8, 8);
    });
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
  if (selection.kind === 'tee') {
    say('Примыкание удаляется вместе с перегородкой: выделите её саму.');
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
  const hit = state.core.pick(world[0], world[1], PICK_PX / view.scale);
  state.selection = hit.kind === 'none' ? null : hit;
  refresh();
  say('');
}

/** Нажатие в «Выборе»: предмет под курсором тянется; комната и пустое место — сдвиг вида. */
function startDrag(world) {
  if (!state.core || tools.drawing) return null;
  const hit = state.core.pick(world[0], world[1], PICK_PX / view.scale);
  if (!DRAGGABLE.has(hit.kind) || !state.core.dragBegin(hit, world[0], world[1])) return null;
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

function updateCursor(world) {
  if (tools.drawing) {
    canvas.style.cursor = '';
    return;
  }
  const hit = state.core.pick(world[0], world[1], PICK_PX / view.scale);
  canvas.style.cursor = CURSORS[hit.kind] || '';
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

const TOOL_KEYS = { v: 'select', r: 'rect', c: 'contour', p: 'partition', o: 'opening' };

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
    onDragStart: (world) => startDrag(world),
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
