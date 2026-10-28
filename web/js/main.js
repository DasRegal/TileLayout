// TileLayout в браузере: связывает ядро, план, инструменты, панель, каталоги и файлы.

import { Catalogs, chooseTile } from './catalogs.js';
import { loadCore } from './core.js';
import { Panel, showSummary, zoneData } from './panel.js';
import { PRINT_SCALES, printProject } from './print.js';
import { installIcons } from './icons.js';
import { DEFAULT_LAYERS, drawScene } from './render.js';
import { acceptDroppedFiles, downloadFile, pickFile, readDraft, readSettings, writeDraft, writeSettings } from './storage.js';
import { clearLocationProject, projectFromLocation, shareLink } from './share.js';
import { TOOL_HINTS, Tools } from './tools.js';
import { attachViewControls, View } from './view.js';
import { drawRoomWalls, wallsBounds } from './walls.js';

const PICK_PX = 6; // допуск выбора предмета, пиксели экрана
const SNAP_PX = 10; // допуск притягивания при перетаскивании
const DRAGGABLE = new Set(['vertex', 'wall', 'partition', 'node', 'tee', 'opening', 'threshold', 'ruler', 'joint', 'pipe', 'panno', 'fixture', 'drain']);
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
    dividerGap: 2,
    fixtureKind: 'bath',
    drainKind: 'point',
    calibration: 0, // калибровка подложки: настоящее расстояние между точками, мм
    pipeDiameter: 50,
    keepAngles: true,
    house: { width: 6000, height: 4000 },
    print: { tiles: true, dims: true, clips: true, cuts: true, calc: true, estimate: false, mono: false, landscape: true, scale: 0 },
    ...readSettings(),
  },
  numbers: null, // номера плиток (tl_web_tile_numbers), когда слой включён
  tabs: [], // открытые вкладки помещений — id комнат
  tab: 0, // открытая вкладка: 0 — план, иначе id комнаты (развёртка её стен)
  walls: null, // развёртка открытой комнаты (tl_web_room_walls)
  wallsFitted: true, // вид развёртки вписан, пока его не двигали
  wallRuler: null, // инструмент «Линейка» на развёртке: {wall, start: [x, h]}, курсор — hover
  hover: null, // курсор на развёртке (притянутый)
};

state.settings.layers = { ...DEFAULT_LAYERS, ...state.settings.layers };

const canvas = document.getElementById('plan');
const ctx = canvas.getContext('2d');
const view = new View();
const wallsCanvas = document.getElementById('walls');
const wallsCtx = wallsCanvas.getContext('2d');
const wallsView = new View();
const room3dCanvas = document.getElementById('room3d');
const room3dCtx = room3dCanvas.getContext('2d');
/** 3D во вкладке помещения: камера каждой комнаты — {yaw, pitch, distance} (нет — по умолчанию). */
const volume = { on: false, cameras: {}, drag: null };
const status = document.getElementById('status');
let frame = 0;

/* выделенное на развёртке стен: стена, отверстие, линейка — у комнаты selection.space, номер — index */
const WALL_ITEMS = new Set(['room_wall', 'wall_pipe', 'wall_ruler']);
/* у комнаты: её настройки на панели — и при выделенном панно или предмете на стене */
const ROOM_ITEMS = new Set(['space', 'panno', ...WALL_ITEMS]);
const selectedSpace = () => (ROOM_ITEMS.has(state.selection?.kind) ? state.selection.space : 0);
/** Выделенная часть пола комнаты (между линиями раздела) или −1. */
const selectedRegion = () => (state.selection?.kind === 'space' && !state.selection.more?.length && state.selection.part > 0
  ? state.selection.part - 1 : -1);
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
  setZoneField: (zone, key, value) => editRoom(() => (zone === 'area'
    ? state.core.setZoneFields([selectedSpace()], `area:${selectedRegion()}`, { [key]: value })
    : state.core.setZoneFields(selectedSpaces(), zone, { [key]: value }))),
  setAreaCovering: (covering) => editRoom(() => state.core.setArea(selectedSpace(), selectedRegion(), covering)),
  renameArea: (name) => editRoom(() => state.core.setArea(selectedSpace(), selectedRegion(), '', name)),
  setWalls: (key, value) => editRoom(() => state.core.setWalls(selectedSpaces(), { [key]: value })),
  openWalls: () => openRoomTab(selectedSpace()),
  addPanno: () => addPanno(),
  /** «Швы как у пола» (у выделенной стены или у всех), «От левого угла». */
  wallAction: (action) => {
    const selection = state.selection;
    if (action === 'fixture_rotate') {
      const object = state.object;
      if (state.core.setFixture(selection.index, { angle: (object.angle + 90) % 360 })) changed();
      return;
    }
    if (action === 'panno_texture') {
      pickPannoTexture();
      return;
    }
    if (action === 'panno_color') {
      if (state.core.setPanno(selection.space, selection.index, { texture: '' })) changed();
      return;
    }
    const wall = selection?.kind === 'room_wall' ? selection.index : -1;
    if (action === 'reset' && wall >= 0) {
      if (state.core.setWallOffset(selection.space, wall, 0, 0)) changed();
      return;
    }
    const count = state.core.alignWallsToFloor(selectedSpace(), action === 'floor_all' ? -1 : wall);
    changed();
    say(count ? (action === 'floor_all' ? `Швы стен совмещены со швами пола: ${count} из ${state.walls?.walls.length ?? count}.` : 'Шов стены продолжает шов пола.')
      : 'Швы пола у стены не вдоль неё (или нет облицовки) — совмещать не с чем.');
  },
  chooseTile: () => pickTile(),
  chooseZoneTile: (zone) => pickZoneTile(zone),
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
  underlay: (action, fields) => underlayAction(action, fields),
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
      'opening.opening_kind': () => core.setOpening(index, value, object.height, object.sill),
      'opening.height': () => core.setOpening(index, '', value, object.sill),
      'opening.sill': () => core.setOpening(index, '', object.height, value),
      'opening.reveal': () => core.setOpeningReveal(index, value),
      'room_wall.offset_x': () => core.setWallOffset(selection.space, index, value, object.offset_y),
      'room_wall.offset_y': () => core.setWallOffset(selection.space, index, object.offset_x, value),
      'panno.columns': () => core.setPanno(selection.space, index, { columns: value }),
      'panno.rows': () => core.setPanno(selection.space, index, { rows: value }),
      'panno.w': () => core.setPanno(selection.space, index, { w: value }),
      'panno.h': () => core.setPanno(selection.space, index, { h: value }),
      'panno.gap': () => core.setPanno(selection.space, index, { gap: value }),
      'panno.x': () => core.setPanno(selection.space, index, { x: value }),
      'panno.y': () => core.setPanno(selection.space, index, { y: value }),
      'panno.price': () => core.setPanno(selection.space, index, { price: value }),
      'panno.color': () => core.setPanno(selection.space, index, { color: value }),
      'wall_pipe.diameter': () => core.setWallPipe(selection.space, index, object.wall, [object.x, object.h], value),
      'wall_pipe.x': () => core.setWallPipe(selection.space, index, object.wall, [value, object.h], object.diameter),
      'wall_pipe.h': () => core.setWallPipe(selection.space, index, object.wall, [object.x, value], object.diameter),
      'pipe.diameter': () => core.setPipe(index, object.x, object.y, value),
      'drain.drain_kind': () => core.setDrain(index, { kind: value }),
      'drain.size': () => core.setDrain(index, { size: value }),
      'drain.length': () => core.setDrain(index, { length: value }),
      'drain.angle': () => core.setDrain(index, { angle: value }),
      'drain.slope': () => core.setDrain(index, { slope: value }),
      'drain.envelope': () => core.setDrain(index, { envelope: value }),
      'fixture.fixture_kind': () => core.setFixture(index, { kind: value }),
      'fixture.w': () => core.setFixture(index, { w: value }),
      'fixture.d': () => core.setFixture(index, { d: value }),
      'fixture.h': () => core.setFixture(index, { h: value }),
      'fixture.angle': () => core.setFixture(index, { angle: value }),
      'fixture.under': () => core.setFixture(index, { under: value }),
      'fixture.behind': () => core.setFixture(index, { behind: value }),
      'fixture.steps': () => core.setFixture(index, { steps: value }),
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

/** Содержимое подложки (tl_web_underlay) — заново, когда в сцене сменилась её "revision". */
const underlay = { revision: 0, image: null, size: [0, 0], lines: null };

function syncUnderlay() {
  const place = state.scene.underlay;
  if (!place || place.revision === underlay.revision) return;
  underlay.revision = place.revision;
  underlay.image = null;
  underlay.lines = null;
  if (!place.present) return;
  const full = state.core.underlay();
  if (full.image) {
    const image = new Image();
    image.onload = requestDraw;
    image.src = full.image;
    underlay.image = image;
    underlay.size = [full.image_w, full.image_h];
  } else {
    underlay.lines = full.lines;
  }
}

/** Габарит подложки на плане или null. */
function underlayBounds() {
  const place = state.scene?.underlay;
  if (!place?.present || !place.visible) return null;
  let local = null;
  if (underlay.image) local = [0, 0, ...underlay.size];
  else if (underlay.lines) {
    local = [Infinity, Infinity, -Infinity, -Infinity];
    for (const line of underlay.lines) {
      for (let i = 0; i < line.length; i += 2) {
        local = [Math.min(local[0], line[i]), Math.min(local[1], line[i + 1]), Math.max(local[2], line[i]), Math.max(local[3], line[i + 1])];
      }
    }
  }
  if (!local) return null;
  const [x, y] = place.origin;
  return local.map((value, i) => (i % 2 ? y : x) + value * place.scale);
}

const MAX_UNDERLAY_SIDE = 2400; // пикселей: фото уменьшается и хранится в JPEG (как у программы)

/** Фото плана — подложкой: уменьшить, сжать в JPEG и отдать ядру. */
async function loadUnderlayImage(file) {
  const url = URL.createObjectURL(file);
  try {
    const image = new Image();
    await new Promise((resolve, reject) => {
      image.onload = resolve;
      image.onerror = reject;
      image.src = url;
    });
    const zoom = Math.min(1, MAX_UNDERLAY_SIDE / Math.max(image.naturalWidth, image.naturalHeight));
    const canvasImage = document.createElement('canvas');
    canvasImage.width = Math.max(1, Math.round(image.naturalWidth * zoom));
    canvasImage.height = Math.max(1, Math.round(image.naturalHeight * zoom));
    const context = canvasImage.getContext('2d');
    context.fillStyle = '#ffffff';
    context.fillRect(0, 0, canvasImage.width, canvasImage.height);
    context.drawImage(image, 0, 0, canvasImage.width, canvasImage.height);
    return state.core.setUnderlayImage(canvasImage.toDataURL('image/jpeg', 0.85), canvasImage.width, canvasImage.height);
  } catch {
    return false;
  } finally {
    URL.revokeObjectURL(url);
  }
}

async function underlayAction(action, fields) {
  const core = state.core;
  if (action === 'image' || action === 'dxf') {
    const picked = await pickFile(action === 'dxf' ? '.dxf' : 'image/jpeg,image/png', true);
    if (!picked) return;
    let ok = false;
    if (action === 'image') {
      ok = await loadUnderlayImage(picked.file);
      if (!ok) say('Не удалось открыть картинку — нужен JPEG или PNG.');
    } else {
      try {
        core.loadUnderlayDxf(await picked.file.text());
        ok = true;
      } catch (error) {
        say(`Не удалось открыть DXF: ${error.message}.`);
      }
    }
    if (ok) {
      changed();
      fitView();
      say(action === 'dxf' ? 'Подложка — DXF-план. Стены притягиваются к его углам и линиям.'
        : 'Подложка — фото. Откалибруйте масштаб по известному размеру.');
    }
  } else if (action === 'calibrate' || action === 'move') {
    setTool(action === 'move' ? 'underlay_move' : 'underlay_calibrate');
  } else if (action === 'remove') {
    core.clearUnderlay();
    changed();
  } else if (action === 'set' && core.setUnderlay(fields)) {
    changed();
  }
}

function requestDraw() {
  if (!frame) frame = requestAnimationFrame(draw);
}

function draw() {
  frame = 0;
  const dpr = window.devicePixelRatio || 1;
  if (state.tab && volume.on) {
    drawRoom3d(dpr);
    return;
  }
  if (state.tab) {
    drawWallsTab(dpr);
    return;
  }
  const box = canvas.getBoundingClientRect();
  const width = Math.round(box.width * dpr);
  const height = Math.round(box.height * dpr);
  if (canvas.width !== width || canvas.height !== height) {
    canvas.width = width;
    canvas.height = height;
  }
  drawScene(ctx, state.scene, view, {
    selected: state.selection?.kind === 'panno' ? [] : selectedSpaces(), // у панно — его рамка
    highlight: highlightOf(state.object),
    image: (ref) => catalogs.image(ref),
    dpr,
    layers: state.settings.layers,
    numbers: state.numbers,
    underlay,
    overlay: (c) => {
      drawRegion(c);
      drawHandles(c);
      tools.draw(c);
    },
  });
}

/** Вкладка помещения в 3D: многоугольники от ядра (tl_room3d.h) — в порядке рисования. */
function drawRoom3d(dpr) {
  const box = room3dCanvas.getBoundingClientRect();
  const width = Math.round(box.width * dpr);
  const height = Math.round(box.height * dpr);
  if (room3dCanvas.width !== width || room3dCanvas.height !== height) {
    room3dCanvas.width = width;
    room3dCanvas.height = height;
  }
  const c = room3dCtx;
  c.setTransform(dpr, 0, 0, dpr, 0, 0);
  const sky = c.createLinearGradient(0, 0, 0, box.height);
  sky.addColorStop(0, '#e0e5ed');
  sky.addColorStop(1, '#f7f7fa');
  c.fillStyle = sky;
  c.fillRect(0, 0, box.width, box.height);
  if (box.width <= 0 || box.height <= 0) return;
  const result = state.core.room3d(state.tab, volume.cameras[state.tab] ?? null, box.width, box.height);
  if (result.camera) volume.cameras[state.tab] = result.camera;
  c.lineJoin = 'round';
  for (const paint of result.paints) {
    c.beginPath();
    for (const contour of paint.c) {
      c.moveTo(contour[0], contour[1]);
      for (let i = 2; i < contour.length; i += 2) c.lineTo(contour[i], contour[i + 1]);
      c.closePath();
    }
    c.fillStyle = paint.f;
    c.fill('evenodd');
    if (paint.s) {
      c.strokeStyle = paint.s;
      c.lineWidth = paint.w;
      c.stroke();
    }
  }
}

/** 2D | 3D во вкладке помещения. */
function setVolume(on) {
  volume.on = on;
  showCanvases();
  say(on ? 'Тяните — повернуть, колесо или кнопки — ближе и дальше, двойной щелчок — как было.' : '');
  requestDraw();
}

/** 3D: тянут — вращают камеру, колесо — ближе и дальше, двойной щелчок — камера по умолчанию. */
function bindRoom3d() {
  room3dCanvas.addEventListener('pointerdown', (event) => {
    const camera = volume.cameras[state.tab];
    if (!camera) return;
    room3dCanvas.setPointerCapture(event.pointerId);
    volume.drag = { x: event.clientX, y: event.clientY, yaw: camera.yaw, pitch: camera.pitch };
  });
  room3dCanvas.addEventListener('pointermove', (event) => {
    const camera = volume.cameras[state.tab];
    if (!volume.drag || !camera) return;
    camera.yaw = (volume.drag.yaw - (event.clientX - volume.drag.x) * 0.35) % 360;
    camera.pitch = Math.min(89, Math.max(5, volume.drag.pitch + (event.clientY - volume.drag.y) * 0.35));
    requestDraw();
  });
  const stop = () => {
    volume.drag = null;
  };
  room3dCanvas.addEventListener('pointerup', stop);
  room3dCanvas.addEventListener('pointercancel', stop);
  room3dCanvas.addEventListener('wheel', (event) => {
    event.preventDefault();
    zoomBy(Math.pow(1.15, -Math.sign(event.deltaY)));
  }, { passive: false });
  room3dCanvas.addEventListener('dblclick', () => {
    delete volume.cameras[state.tab];
    requestDraw();
  });
  for (const button of document.querySelectorAll('#mode-switch [data-mode]')) {
    button.addEventListener('click', () => setVolume(button.dataset.mode === 'volume'));
  }
  new ResizeObserver(() => requestDraw()).observe(room3dCanvas);
}

/** Вкладка помещения: развёртка его стен. */
function drawWallsTab(dpr) {
  const box = wallsCanvas.getBoundingClientRect();
  const width = Math.round(box.width * dpr);
  const height = Math.round(box.height * dpr);
  if (wallsCanvas.width !== width || wallsCanvas.height !== height) {
    wallsCanvas.width = width;
    wallsCanvas.height = height;
  }
  if (state.wallsFitted && state.walls && box.width > 0) wallsView.fit(wallsBounds(state.walls), box.width, box.height, 24);
  const selection = state.selection;
  const marks = selection && WALL_ITEMS.has(selection.kind) && selection.space === state.tab
    ? { [{ room_wall: 'wall', wall_pipe: 'pipe', wall_ruler: 'ruler' }[selection.kind]]: selection.index }
    : selection?.kind === 'panno' && selection.space === state.tab ? { panno: selection.index }
    : {};
  const ruler = state.wallRuler;
  drawRoomWalls(wallsCtx, state.walls, wallsView, {
    dpr,
    image: (ref) => catalogs.image(ref),
    layers: state.settings.layers,
    marks,
    preview: ruler && state.hover ? [toDevelopment(ruler.wall, ruler.start), state.hover] : null,
  });
}

// ---- вкладки: план и стены помещений ------------------------------------------------

const spaceName = (id) => state.scene?.spaces.find((space) => space.id === id)?.name ?? 'Комната';

function openRoomTab(id) {
  if (!id) return;
  if (!state.tabs.includes(id)) state.tabs.push(id);
  showTab(id);
}

function closeRoomTab(id) {
  state.tabs = state.tabs.filter((tab) => tab !== id);
  if (state.tab === id) showTab(0);
  else renderTabs();
}

/** Показать план или развёртку (холсты, переключатель 2D | 3D, кнопка «спасибо»). */
function showCanvases() {
  const walls = Boolean(state.tab);
  canvas.hidden = walls;
  wallsCanvas.hidden = !walls || volume.on;
  room3dCanvas.hidden = !walls || !volume.on;
  document.getElementById('mode-switch').hidden = !walls;
  for (const button of document.querySelectorAll('#mode-switch [data-mode]')) {
    button.setAttribute('aria-pressed', String((button.dataset.mode === 'volume') === volume.on));
  }
  document.querySelector('.thanks').hidden = walls;
}

/** Перейти на вкладку: у помещения — выделить его комнату (панель — её настройки и стены). */
function showTab(id) {
  if (state.tab !== id) state.wallsFitted = true;
  state.tab = id;
  state.wallRuler = null;
  if (id && selectedSpace() !== id) state.selection = { kind: 'space', space: id };
  else if (!id && WALL_ITEMS.has(state.selection?.kind)) state.selection = { kind: 'space', space: state.selection.space };
  showCanvases();
  if (id) say('Развёртка стен — как видно из комнаты. Колесо или два пальца — масштаб, двойной щелчок — вписать.');
  refresh();
}

function renderTabs() {
  const strip = document.getElementById('tab-strip');
  const select = document.getElementById('tab-select');
  document.getElementById('room-tabs').hidden = state.tabs.length === 0;
  const tab = (id, title) => {
    const button = document.createElement('button');
    button.type = 'button';
    button.className = id ? 'tab' : 'tab plan';
    button.setAttribute('role', 'tab');
    button.setAttribute('aria-selected', String(state.tab === id));
    button.textContent = title;
    button.addEventListener('click', () => showTab(id));
    if (id) {
      const close = document.createElement('span');
      close.className = 'close';
      close.textContent = '×';
      close.title = 'Закрыть вкладку';
      close.addEventListener('click', (event) => {
        event.stopPropagation();
        closeRoomTab(id);
      });
      button.append(close);
    }
    return button;
  };
  strip.replaceChildren(tab(0, 'План'), ...state.tabs.map((id) => tab(id, `Стены: ${spaceName(id)}`)));
  select.replaceChildren(new Option('План', '0'), ...state.tabs.map((id) => new Option(`Стены: ${spaceName(id)}`, String(id))));
  select.value = String(state.tab);
}

/** Контуры выделенного предмета для подсветки. */
function highlightOf(object) {
  return object ? object.outline : [];
}

/** Выделенная часть пола (участок) — заливка и рамка, в пикселях экрана. */
function drawRegion(c) {
  const region = selectedRegion();
  if (region < 0) return;
  const outline = state.core.spaceTile(selectedSpace())?.regions[region]?.outline;
  if (!outline) return;
  c.beginPath();
  for (let i = 0; i < outline.length; i += 2) {
    const [x, y] = view.toScreen(outline[i], outline[i + 1]);
    if (i) c.lineTo(x, y);
    else c.moveTo(x, y);
  }
  c.closePath();
  c.fillStyle = 'rgba(37, 99, 235, 0.16)';
  c.fill();
  c.strokeStyle = '#2563eb';
  c.lineWidth = 1.6;
  c.stroke();
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
  const camera = volume.cameras[state.tab];
  if (state.tab && volume.on) { // 3D: ближе — меньше расстояние
    if (camera) camera.distance = Math.min(200000, Math.max(300, camera.distance / factor));
    requestDraw();
    return;
  }
  const target = state.tab ? wallsCanvas : canvas;
  const box = target.getBoundingClientRect();
  (state.tab ? wallsView : view).zoomAt(box.width / 2, box.height / 2, factor);
  if (state.tab) state.wallsFitted = false;
  requestDraw();
}

function fitView() {
  if (state.tab && volume.on) { // те же углы, вся комната в кадре
    const camera = volume.cameras[state.tab];
    if (camera) camera.distance = 0;
    requestDraw();
    return;
  }
  if (state.tab) {
    state.wallsFitted = true;
    requestDraw();
    return;
  }
  const box = canvas.getBoundingClientRect();
  const extra = underlayBounds(); // на экране — и подложка
  const plan = state.scene?.bounds;
  const hasPlan = plan && plan[2] > plan[0];
  const bounds = extra && hasPlan
    ? [Math.min(plan[0], extra[0]), Math.min(plan[1], extra[1]), Math.max(plan[2], extra[2]), Math.max(plan[3], extra[3])]
    : extra || plan;
  if (bounds) view.fit(bounds, box.width, box.height);
  requestDraw();
}

// ---- состояние ------------------------------------------------------------------

let summaryTimer = 0;

/** После любого изменения: сцена, выделение, панель, расчёт, кнопки. */
function refresh() {
  const core = state.core;
  state.scene = core.scene();
  syncUnderlay();
  const selection = state.selection;
  const exists = (id) => state.scene.spaces.some((space) => space.id === id);
  if (selection?.kind === 'space' && !exists(selection.space)) {
    state.selection = null;
  } else if (selection?.more) {
    selection.more = selection.more.filter(exists);
  }
  if (state.tab && !exists(state.tab)) {
    state.tab = 0;
    showCanvases();
  }
  state.walls = state.tab ? core.roomWalls(state.tab) : null;
  if (WALL_ITEMS.has(state.selection?.kind)) {
    state.object = state.selection.space === state.tab ? wallObject(state.selection) : null;
  } else if (state.selection?.kind === 'panno') {
    state.object = pannoObject(state.selection);
  } else {
    state.object = state.selection && state.selection.kind !== 'space' ? core.object(state.selection) : null;
  }
  if (state.object?.kind === 'wall') state.object.keep_angles = state.settings.keepAngles;
  state.numbers = state.settings.layers.numbers ? core.tileNumbers() : null;
  state.tabs = state.tabs.filter(exists); // комнаты больше нет — закрыть её вкладку
  renderTabs();
  if (state.selection && state.selection.kind !== 'space' && !state.object) state.selection = null;
  panel.show(selectedSpaces().map((id) => core.spaceTile(id)), catalogs, state.multi, Boolean(state.tab), selectedRegion());
  panel.showObject(state.object);
  panel.showTool(tools.tool, state.settings, TOOL_HINTS[tools.tool]);
  panel.showUnderlay(state.scene.underlay);
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
  if (selection.kind === 'room_wall') {
    say('Стены образуют план: правьте их на вкладке «План».');
    return;
  }
  if (WALL_ITEMS.has(selection.kind) || selection.kind === 'panno') {
    if (state.core.deleteWallItem(selection.space, selection.kind, selection.index)) {
      state.selection = { kind: 'space', space: selection.space };
      changed();
      say('Удалено. Отменить — Ctrl+Z.');
    }
    return;
  }
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

const WALL_TOOLS = new Set(['select', 'ruler', 'pipe']); // работают и на развёртке

function setTool(tool) {
  state.wallRuler = null;
  if (state.tab && !WALL_TOOLS.has(tool)) { // рисуют план — на план
    state.tab = 0;
    showCanvases();
  }
  if (tool !== 'select' && volume.on) { // рисуют — в 3D нельзя: к развёртке
    volume.on = false;
    showCanvases();
  }
  tools.set(tool);
  document.body.classList.toggle('drawing', tool !== 'select');
  for (const button of document.querySelectorAll('[data-tool]')) {
    button.setAttribute('aria-pressed', String(button.dataset.tool === tool));
  }
  if (tool !== 'select') {
    state.selection = state.tab ? { kind: 'space', space: state.tab } : null;
    state.multi = false;
  }
  if (state.tab && tool !== 'select') say(tool === 'ruler' ? 'Линейка на стене: два щелчка на одной стене.' : 'Отверстие на стене: щёлкните, где оно.');
  refresh();
}

// ---- развёртка стен: предметы, инструменты, перетаскивание ------------------------------------

/** Точка на стене wall ([x от левого угла, h от пола]) → развёртка. */
function toDevelopment(wall, [x, h]) {
  return [state.walls.walls[wall].x + x, state.walls.height - h];
}

/** Точка развёртки → {wall, at}: ближайшая стена (и в зазоре между стенами); null — далеко. */
function fromDevelopment([x, y]) {
  const data = state.walls;
  if (!data || y < -200 || y > data.height) return null;
  let best = null;
  let bestDistance = 200; // половина зазора между стенами
  data.walls.forEach((wall, i) => {
    const distance = Math.max(0, wall.x - x, x - wall.x - wall.length);
    if (distance <= bestDistance) {
      best = i;
      bestDistance = distance;
    }
  });
  if (best === null) return null;
  const wall = data.walls[best];
  return { wall: best, at: [Math.min(Math.max(x - wall.x, 0), wall.length), Math.min(Math.max(data.height - y, 0), data.height)] };
}

/** Стена под точкой (на самой стене). */
function wallAt([x, y]) {
  const data = state.walls;
  return data.walls.findIndex((wall) => x >= wall.x && x <= wall.x + wall.length && y >= 0 && y <= data.height);
}

const wallsSnap = (world, free) => (free ? world : state.core.wallsSnap(state.tab, world[0], world[1], SNAP_PX / wallsView.scale));

/** Отверстие или линейка (конец, целиком) под точкой. */
function pickOnWalls(world) {
  const data = state.walls;
  const [sx, sy] = wallsView.toScreen(...world);
  const near = (point, pixels) => {
    const [px, py] = wallsView.toScreen(...point);
    return Math.hypot(px - sx, py - sy) <= pixels;
  };
  for (let i = data.pannos.length - 1; i >= 0; i--) { // панно — прямоугольник в развёртке
    const [x0, y0, , , x1, y1] = data.pannos[i].outline;
    if (world[0] >= Math.min(x0, x1) && world[0] <= Math.max(x0, x1) && world[1] >= Math.min(y0, y1) && world[1] <= Math.max(y0, y1)) {
      return { kind: 'panno', index: data.pannos[i].index, panno: data.pannos[i] };
    }
  }
  for (let i = data.pipes.length - 1; i >= 0; i--) {
    const pipe = data.pipes[i];
    if (pipe.wall !== null && near(pipe.point, Math.max((pipe.diameter * wallsView.scale) / 2, 4) + PICK_PX)) return { kind: 'wall_pipe', index: i };
  }
  for (let i = data.rulers.length - 1; i >= 0; i--) {
    const ruler = data.rulers[i];
    if (ruler.wall === null) continue;
    if (near(ruler.a, PICK_PX + 2)) return { kind: 'wall_ruler', index: i, end: 1 };
    if (near(ruler.b, PICK_PX + 2)) return { kind: 'wall_ruler', index: i, end: 2 };
    const [ax, ay] = wallsView.toScreen(...ruler.a);
    const [bx, by] = wallsView.toScreen(...ruler.b);
    const length = Math.hypot(bx - ax, by - ay) || 1;
    const t = Math.min(1, Math.max(0, ((sx - ax) * (bx - ax) + (sy - ay) * (by - ay)) / (length * length)));
    if (Math.hypot(ax + t * (bx - ax) - sx, ay + t * (by - ay) - sy) <= PICK_PX) return { kind: 'wall_ruler', index: i, end: 0 };
  }
  return null;
}

/** Свойства выделенного панно — для панели; outline — рамка на плане (у панно на полу). */
function pannoObject(selection) {
  const room = state.core.spaceTile(selection.space);
  const panno = room?.pannos[selection.index];
  if (!panno) return null;
  const onPlan = state.scene.spaces.find((space) => space.id === selection.space)?.pannos.find((p) => p.index === selection.index);
  return { kind: 'panno', ...panno, outline: onPlan && !state.tab ? [onPlan.outline] : [], handles: [] };
}

/** Добавить панно: на стене — выделенной (или первой), на полу — в центре комнаты. */
function addPanno() {
  const id = selectedSpace();
  if (!id) return;
  const selection = state.selection;
  const wall = !state.tab ? -1 : selection?.kind === 'room_wall' ? selection.index : 0;
  if (state.tab && !state.walls?.enabled) {
    say('Сначала включите облицовку стен.');
    return;
  }
  const index = state.core.addPanno(id, wall);
  if (index >= 0) {
    state.selection = { kind: 'panno', space: id, index };
    changed();
    say(state.tab ? 'Панно на стене — тяните его мышью; угол притягивается к швам.' : 'Панно в центре комнаты — тяните его мышью.');
  }
}

/** Свойства выделенного на развёртке — для панели. */
function wallObject(selection) {
  const data = state.walls;
  if (!data) return null;
  if (selection.kind === 'room_wall') {
    const wall = data.walls[selection.index];
    return wall && { kind: 'room_wall', number: selection.index + 1, length: Math.round(wall.length), offset_x: wall.offset[0], offset_y: wall.offset[1], outline: [], handles: [] };
  }
  if (selection.kind === 'wall_pipe') {
    const pipe = data.pipes[selection.index];
    return pipe && { kind: 'wall_pipe', diameter: pipe.diameter, x: pipe.at[0], h: pipe.at[1], wall: pipe.wall, outline: [], handles: [] };
  }
  const ruler = data.rulers[selection.index];
  return ruler && { kind: 'wall_ruler', length: ruler.length, outline: [], handles: [] };
}

function selectOnWalls(kind, index) {
  state.selection = kind ? { kind, space: state.tab, index } : { kind: 'space', space: state.tab };
  refresh();
}

/** Щелчок на развёртке: инструмент ставит линейку или отверстие; «Выбор» — предмет, стену или комнату. */
function clickOnWalls(world, event) {
  if (!state.walls) return;
  const free = event.shiftKey;
  if (tools.tool === 'pipe' || tools.tool === 'ruler') {
    const point = wallsSnap(world, free);
    const target = fromDevelopment(point);
    if (!target) {
      say('Щёлкните по стене.');
      return;
    }
    if (tools.tool === 'pipe') {
      const index = state.core.addWallPipe(state.tab, target.wall, target.at, state.settings.pipeDiameter);
      if (index >= 0) {
        state.selection = { kind: 'wall_pipe', space: state.tab, index };
        changed();
        say(`Отверстие на стене: ${Math.round(target.at[0])} от левого угла, ${Math.round(target.at[1])} от пола.`);
      }
    } else if (!state.wallRuler) {
      state.wallRuler = { wall: target.wall, start: target.at };
      state.hover = point;
      say('Второй конец линейки — на той же стене; Shift — без притягивания.');
      requestDraw();
    } else {
      const { wall, start } = state.wallRuler;
      const data = state.walls;
      const end = [Math.min(Math.max(point[0] - data.walls[wall].x, 0), data.walls[wall].length),
        Math.min(Math.max(data.height - point[1], 0), data.height)];
      state.wallRuler = null;
      const index = state.core.addWallRuler(state.tab, wall, start, end);
      if (index >= 0) state.selection = { kind: 'wall_ruler', space: state.tab, index };
      changed();
    }
    return;
  }
  const hit = pickOnWalls(world);
  if (hit) {
    selectOnWalls(hit.kind, hit.index);
    return;
  }
  const wall = wallAt(world);
  selectOnWalls(wall >= 0 ? 'room_wall' : null, wall);
}

/** Нажатие на развёртке: тянут отверстие, линейку (за конец — с притягиванием) или, с Ctrl, плитку стены. */
function dragOnWalls(world, event) {
  if (!state.walls || tools.tool !== 'select') return null;
  const core = state.core;
  const id = state.tab;
  const hit = pickOnWalls(world);
  const wall = wallAt(world);
  if (!hit && !((event.ctrlKey || event.metaKey) && wall >= 0)) return null; // сдвиг вида
  const start = world;
  let grab;
  if (!hit) {
    const origin = state.walls.walls[wall].offset;
    selectOnWalls('room_wall', wall);
    grab = (point) => {
      const x = origin[0] + point[0] - start[0];
      const y = origin[1] - (point[1] - start[1]);
      say(`Плитка стены сдвинута на ${Math.round(x)} × ${Math.round(y)} мм.`);
      return core.setWallOffset(id, wall, x, y);
    };
  } else if (hit.kind === 'panno') { // левый нижний угол — с притягиванием к швам и углам
    const panno = hit.panno;
    const origin = toDevelopment(panno.wall, panno.at);
    selectOnWalls(hit.kind, hit.index);
    grab = (point, free) => {
      const target = fromDevelopment(wallsSnap([origin[0] + point[0] - start[0], origin[1] + point[1] - start[1]], free));
      return target && core.setPanno(id, hit.index, { wall: target.wall, x: target.at[0], y: target.at[1] });
    };
  } else if (hit.kind === 'wall_pipe') {
    const pipe = state.walls.pipes[hit.index];
    const origin = pipe.point;
    selectOnWalls(hit.kind, hit.index);
    grab = (point, free) => {
      const target = fromDevelopment(wallsSnap([origin[0] + point[0] - start[0], origin[1] + point[1] - start[1]], free));
      return target && core.setWallPipe(id, hit.index, target.wall, target.at, pipe.diameter);
    };
  } else {
    const ruler = state.walls.rulers[hit.index];
    selectOnWalls(hit.kind, hit.index);
    grab = (point, free) => {
      const shift = [point[0] - start[0], -(point[1] - start[1])];
      const moved = (at, end) => {
        if (hit.end !== 0 && hit.end !== end) return at;
        const next = [at[0] + shift[0], at[1] + shift[1]];
        if (hit.end === 0) return next;
        const snapped = wallsSnap(toDevelopment(ruler.wall, next), free);
        return [snapped[0] - state.walls.walls[ruler.wall].x, state.walls.height - snapped[1]];
      };
      return core.setWallRuler(id, hit.index, moved(ruler.start, 1), moved(ruler.end, 2));
    };
  }
  return {
    move(point, moveEvent) {
      if (grab(point, moveEvent.shiftKey)) {
        state.walls = core.roomWalls(id);
        state.object = state.selection.kind === 'panno' ? pannoObject(state.selection) : wallObject(state.selection);
        panel.showObject(state.object);
        requestDraw();
      }
    },
    end(moved) {
      if (moved) changed();
    },
  };
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

/** Картинка выделенного панно из каталога (на всё панно) и её цвет. */
async function pickPannoTexture() {
  const selection = state.selection;
  if (selection?.kind !== 'panno') return;
  const panno = state.core.spaceTile(selection.space).pannos[selection.index];
  const choice = await chooseTile(catalogs, panno.texture);
  if (choice === 'color-only') {
    if (state.core.setPanno(selection.space, selection.index, { texture: '' })) changed();
  } else if (choice) {
    const fields = await catalogs.fieldsFor(choice.catalog, choice.tile);
    if (state.core.setPanno(selection.space, selection.index, { texture: fields.texture, color: fields.color ?? panno.color })) changed();
  }
}

/** Плитка зоны пола (бордюр, ковёр) из каталога: размер, рисунок и цвет. */
async function pickZoneTile(zone) {
  if (!selectedSpace()) return;
  const room = state.core.spaceTile(selectedSpace());
  room.region = selectedRegion();
  const choice = await chooseTile(catalogs, zoneData(room, zone).tile.texture);
  const [ids, key] = zone === 'area' ? [[selectedSpace()], `area:${selectedRegion()}`] : [selectedSpaces(), zone];
  if (choice === 'color-only') {
    editRoom(() => state.core.setZoneFields(ids, key, { texture: '' }));
  } else if (choice) {
    const fields = await catalogs.fieldsFor(choice.catalog, choice.tile);
    editRoom(() => state.core.setZoneFields(ids, key, fields));
  }
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
  share: () => shareProject(),
  'dxf-plan': () => exportDxf(false),
  'dxf-cuts': () => exportDxf(true),
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

/** Ссылка на проект (share.js): показать и скопировать. */
async function shareProject() {
  const dialog = document.getElementById('share-dialog');
  const field = document.getElementById('share-link');
  const note = document.getElementById('share-note');
  let link;
  try {
    link = await shareLink(state.core.shareText());
  } catch {
    say('Этот браузер не умеет сжимать проект для ссылки — обновите его или сохраните проект файлом.');
    return;
  }
  field.value = link;
  note.textContent = link.length > 30000
    ? `Ссылка длинная (${Math.round(link.length / 1000)} тыс. знаков): мессенджер может её обрезать — тогда отправьте файл проекта.`
    : `Длина ссылки — ${link.length} знаков.`;
  dialog.showModal();
  field.select();
  try {
    await navigator.clipboard.writeText(link);
    say('Ссылка на проект скопирована.');
  } catch {
    say('Скопируйте ссылку из окна.');
  }
}

/** Проект по ссылке: прошлый черновик — в запас, кнопка «Вернуть мой проект». */
const DRAFT_BACKUP_KEY = 'tilelayout.draft-before-link';

async function openFromLink() {
  let text;
  try {
    text = await projectFromLocation();
  } catch {
    clearLocationProject();
    say('Ссылка на проект повреждена — попросите прислать её ещё раз.');
    return false;
  }
  if (!text) return false;
  clearLocationProject();
  const draft = readDraft();
  try {
    state.core.open(text);
  } catch (error) {
    say(`Не удалось открыть проект по ссылке: ${error.message}`);
    return false;
  }
  state.fileName = 'проект-по-ссылке.tilelayout.json';
  const backed = draft && (() => {
    try {
      localStorage.setItem(DRAFT_BACKUP_KEY, JSON.stringify(draft));
      return true;
    } catch {
      return false; // нет места — без кнопки «Вернуть»
    }
  })();
  writeDraft(state.core.save(), state.fileName); // перезагрузка страницы — тот же проект
  document.getElementById('restore-draft').hidden = !backed;
  return true;
}

function restoreDraft() {
  try {
    const draft = JSON.parse(localStorage.getItem(DRAFT_BACKUP_KEY));
    state.core.open(draft.text);
    state.fileName = draft.name;
    localStorage.removeItem(DRAFT_BACKUP_KEY);
  } catch {
    say('Прошлый проект не нашёлся.');
    return;
  }
  document.getElementById('restore-draft').hidden = true;
  state.selection = null;
  changed();
  fitView();
  say('Вернули ваш проект.');
}

/** Чертёж DXF — план или подрезки для станка (tl_dxf.h). */
function exportDxf(cuts) {
  const name = `${baseName()}${cuts ? '-подрезки' : ''}.dxf`;
  downloadFile(state.core.dxf(cuts), name, 'application/dxf');
  say(`Чертёж сохранён: ${name} — в папке загрузок браузера.`);
}

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
  const checks = ['tiles', 'dims', 'clips', 'cuts', 'calc', 'estimate', 'mono'];
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

const TOOL_KEYS = { v: 'select', r: 'rect', c: 'contour', p: 'partition', o: 'opening', j: 'joint', h: 'pipe', l: 'ruler', d: 'divider', b: 'fixture', g: 'drain' };

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
    if (command) {
      button.addEventListener('click', () => {
        button.closest('details')?.removeAttribute('open'); // пункт меню «Экспорт» — меню закрыть
        command();
      });
    }
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
    if (hit.kind === 'space') { // двойной щелчок по комнате — её стены
      openRoomTab(hit.space);
      return;
    }
    if (hit.kind === 'wall' && state.core.insertVertex(hit.index, hit.part, x, y, state.settings.grid)) {
      state.selection = { kind: 'vertex', index: hit.index, part: hit.part + 1, end: 0 };
      changed();
      say('Новый угол на стене — тяните его.');
    }
  });
  attachViewControls(wallsCanvas, wallsView, {
    onChange: () => {
      state.wallsFitted = false;
      requestDraw();
    },
    onClick: (world, event) => {
      if (state.core) clickOnWalls(world, event);
    },
    onHover: (world, event) => {
      if (!state.wallRuler) return;
      state.hover = wallsSnap(world, event.shiftKey);
      requestDraw();
    },
    onDragStart: (world, event) => dragOnWalls(world, event),
  });
  wallsCanvas.addEventListener('dblclick', () => fitView());
  document.getElementById('tab-select').addEventListener('change', (event) => showTab(Number(event.target.value)));
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
  new ResizeObserver(() => requestDraw()).observe(wallsCanvas);
  bindRoom3d();
  document.getElementById('restore-draft').addEventListener('click', restoreDraft);
  document.getElementById('share-copy').addEventListener('click', async () => {
    const field = document.getElementById('share-link');
    field.select();
    try {
      await navigator.clipboard.writeText(field.value);
      say('Ссылка на проект скопирована.');
    } catch {
      document.execCommand('copy'); // старые браузеры
    }
  });
  window.addEventListener('hashchange', async () => { // ссылку открыли в уже открытой вкладке
    if (await openFromLink()) {
      state.selection = null;
      changed();
      fitView();
      say('Открыт проект по ссылке.');
    }
  });
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
  if (await openFromLink()) {
    message = 'Открыт проект по ссылке.' + (document.getElementById('restore-draft').hidden ? '' : ' Ваш прошлый проект — кнопка «Вернуть мой проект».');
  } else if (draft) {
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
  say(navigator.onLine ? message : `Нет интернета — сайт работает из сохранённой копии. ${message}`);
  registerOffline();
}

/** Офлайн-режим (web/sw.js): сайт целиком в кэше браузера — открывается и без интернета. */
function registerOffline() {
  const secure = location.protocol === 'https:' || location.hostname === 'localhost';
  if (!secure || !('serviceWorker' in navigator)) return;
  navigator.serviceWorker.register('sw.js').catch(() => {}); // не вышло — просто без офлайн-режима
}

start();
