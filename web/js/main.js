// TileLayout в браузере: связывает ядро, план, панель, каталоги и файлы.

import { Catalogs, chooseTile } from './catalogs.js';
import { loadCore } from './core.js';
import { Panel, showSummary } from './panel.js';
import { drawScene } from './render.js';
import { acceptDroppedFiles, downloadFile, pickFile, readDraft, writeDraft } from './storage.js';
import { attachViewControls, View } from './view.js';

const state = {
  core: null,
  scene: null,
  selected: 0, // id комнаты
  fileName: 'plan.tilelayout.json',
};

const canvas = document.getElementById('plan');
const ctx = canvas.getContext('2d');
const view = new View();
const status = document.getElementById('status');
let frame = 0;

const catalogs = new Catalogs(() => requestDraw());
const panel = new Panel(document.getElementById('panel'), {
  setField: (key, value) => edit(() => state.core.setTileField(state.selected, key, value)),
  chooseTile: () => pickTile(),
  colorOnly: () => edit(() => state.core.setTileField(state.selected, 'texture', '')),
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
  drawScene(ctx, state.scene, view, { selected: state.selected, image: (ref) => catalogs.image(ref), dpr });
}

function fitView() {
  const box = canvas.getBoundingClientRect();
  if (state.scene) view.fit(state.scene.bounds, box.width, box.height);
  requestDraw();
}

// ---- состояние ------------------------------------------------------------------

let summaryTimer = 0;

/** После любого изменения проекта: сцена, панель, расчёт, черновик, кнопки. */
function refresh() {
  const core = state.core;
  state.scene = core.scene();
  if (!state.scene.spaces.some((space) => space.id === state.selected)) state.selected = 0;
  panel.show(state.selected ? core.spaceTile(state.selected) : null, catalogs);
  document.querySelector('[data-action="undo"]').disabled = !core.canUndo();
  document.querySelector('[data-action="redo"]').disabled = !core.canRedo();
  clearTimeout(summaryTimer); // расчёт — чуть позже: при вводе подряд не пересчитывать на каждое поле
  summaryTimer = setTimeout(() => showSummary(document.getElementById('summary'), core.summary(), catalogs), 150);
  requestDraw();
}

function saveDraft() {
  if (!writeDraft(state.core.save(), state.fileName)) {
    say('Черновик в браузере не сохранился — сохраните проект файлом.');
  }
}

function edit(change) {
  if (!state.selected) return;
  if (!change()) {
    say('Значение не подходит.');
    return;
  }
  refresh();
  saveDraft();
}

function say(text) {
  status.textContent = text;
}

function openProject({ name, text }) {
  try {
    state.core.open(text);
  } catch (error) {
    say(`Не удалось открыть «${name}»: ${error.message}`);
    return;
  }
  state.fileName = name;
  state.selected = 0;
  refresh();
  fitView();
  saveDraft();
  say(`Открыт «${name}». Щёлкните комнату, чтобы настроить плитку.`);
}

async function pickTile() {
  if (!state.selected) return;
  const room = state.core.spaceTile(state.selected);
  const choice = await chooseTile(catalogs, room.tile.texture);
  if (choice === 'color-only') {
    edit(() => state.core.setTileField(state.selected, 'texture', ''));
  } else if (choice) {
    const fields = await catalogs.fieldsFor(choice.catalog, choice.tile);
    edit(() => state.core.setTileFields(state.selected, fields));
  }
}

// ---- команды -------------------------------------------------------------------

const commands = {
  demo() {
    state.core.newDemo();
    state.fileName = 'plan.tilelayout.json';
    state.selected = 0;
    refresh();
    fitView();
    saveDraft();
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
    if (state.core.undo()) {
      refresh();
      saveDraft();
    }
  },
  redo() {
    if (state.core.redo()) {
      refresh();
      saveDraft();
    }
  },
  fit: fitView,
  catalog: pickTile,
};

function bindCommands() {
  for (const button of document.querySelectorAll('[data-action]')) {
    const command = commands[button.dataset.action];
    if (command) button.addEventListener('click', () => command());
  }
  document.addEventListener('keydown', (event) => {
    if (!(event.ctrlKey || event.metaKey) || document.querySelector('dialog[open]')) return;
    const key = event.key.toLowerCase();
    const command = {
      z: event.shiftKey ? 'redo' : 'undo',
      y: 'redo',
      o: 'open',
      s: 'save',
      k: 'catalog',
    }[key];
    if (!command || (key === 'z' && event.target.matches('input'))) return;
    event.preventDefault();
    commands[command]();
  });
  attachViewControls(canvas, view, {
    onChange: requestDraw,
    onClick: ([x, y]) => {
      state.selected = state.core.spaceAt(x, y);
      refresh();
      if (state.selected) say('');
    },
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
  if (draft) {
    try {
      state.core.open(draft.text);
      state.fileName = draft.name;
      say('Продолжаем последний проект. Новый пример — кнопка «Пример».');
    } catch {
      state.core.newDemo();
    }
  } else {
    state.core.newDemo();
    say('Пример проекта. Щёлкните комнату, чтобы настроить плитку; откройте свой файл кнопкой «Открыть».');
  }
  refresh();
  fitView();
}

start();
