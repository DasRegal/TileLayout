// Панель справа: плитка выбранной комнаты и расчёт к покупке. Поля — по
// таблице FIELDS: ключ как в файле проекта, подпись и пределы — как на панели
// настольной программы.

const ROW_SHIFTS = [
  ['Прямая (без смещения)', 0],
  ['Вразбежку 1/2', 0.5],
  ['Вразбежку 1/3', 1 / 3],
  ['Вразбежку 1/4', 0.25],
];

const SECTIONS = [
  {
    title: 'Плитка',
    fields: [
      { key: 'w', label: 'Длина плитки', min: 10, max: 5000, step: 10, unit: 'мм' },
      { key: 'h', label: 'Ширина плитки', min: 10, max: 5000, step: 10, unit: 'мм' },
      { key: 'gap', label: 'Шов (зазор)', min: 0, max: 50, step: 0.5, unit: 'мм' },
      { key: 'shift', label: 'Раскладка', options: ROW_SHIFTS },
      { key: 'angle', label: 'Поворот', min: -180, max: 180, step: 5, unit: '°' },
      { key: 'off_x', label: 'Смещение X', min: -100000, max: 100000, step: 5, unit: 'мм' },
      { key: 'off_y', label: 'Смещение Y', min: -100000, max: 100000, step: 5, unit: 'мм' },
      { key: 'wall_gap', label: 'Отступ от стены', min: 0, max: 100, step: 1, unit: 'мм' },
      { key: 'min_cut', label: 'Мин. ширина подрезки', min: 0, max: 1000, step: 5, unit: 'мм' },
      { key: 'reuse', label: 'Использовать остатки подрезок', check: true },
      { key: 'cut_allow', label: 'Запас на рез', min: 0, max: 20, step: 0.5, unit: 'мм' },
      { key: 'reserve', label: 'Запас', min: 0, max: 100, step: 1, unit: '%' },
      { key: 'color', label: 'Цвет', color: true },
    ],
  },
  {
    title: 'Клей',
    fields: [
      { key: 'glue_rate', label: 'Расход на 1 мм', min: 0.1, max: 10, step: 0.05, unit: 'кг/м²' },
      { key: 'glue_mm', label: 'Толщина слоя', min: 0.5, max: 50, step: 0.5, unit: 'мм' },
      { key: 'glue_bag', label: 'Масса мешка', min: 1, max: 100, step: 1, unit: 'кг' },
    ],
  },
  {
    title: 'СВП (система выравнивания)',
    fields: [
      { key: 'svp_long', label: 'На длинную сторону', min: 0, max: 20, step: 1, unit: 'шт.' },
      { key: 'svp_short', label: 'На короткую сторону', min: 0, max: 20, step: 1, unit: 'шт.' },
      { key: 'svp_pack', label: 'В упаковке', min: 1, max: 10000, step: 50, unit: 'шт.' },
    ],
  },
];

const TOOL_TITLES = {
  rect: 'Прямоугольник',
  contour: 'Контур',
  partition: 'Перегородка',
  opening: 'Проём',
  ruler: 'Линейка',
  joint: 'Деформационный шов',
};

/* Предметы плана: заголовок, поля (ключ — как в tl_web_object), подсказка, кнопка удаления. */
const THICKNESS = { key: 'thickness', label: 'Толщина', min: 10, max: 1000, step: 10, unit: 'мм' };
const COORDINATES = [
  { key: 'x', label: 'X', min: -1e7, max: 1e7, step: 10, unit: 'мм' },
  { key: 'y', label: 'Y', min: -1e7, max: 1e7, step: 10, unit: 'мм' },
];
const LOCK = { key: 'locked', label: 'Заблокировать от перемещения', check: true, always: true };
const OBJECTS = {
  partition: {
    title: 'Перегородка',
    fields: [
      THICKNESS,
      { key: 'length', label: 'Длина', min: 10, max: 100000, step: 10, unit: 'мм' },
      { key: 'angle', label: 'Угол', min: 0, max: 360, step: 5, unit: '°' },
      LOCK,
    ],
    info: () =>
      'Круг посередине — сдвиг поперёк, квадрат на конце — длина и поворот, ромб на углу — толщина у этого конца.',
    remove: 'Удалить',
  },
  opening: {
    title: 'Проём',
    fields: [
      { key: 'width', label: 'Ширина', min: 100, max: 10000, step: 10, unit: 'мм' },
      { key: 'from_start', label: 'От начала стены', min: 0, max: 1e6, step: 10, unit: 'мм' },
      { key: 'to_end', label: 'До конца стены', min: 0, max: 1e6, step: 10, unit: 'мм' },
      LOCK,
    ],
    info: (o) =>
      ({ wall: 'В основной стене.', partition: 'В перегородке.', none: 'Не на стене — сдвиньте или удалите.' })[o.host] +
      ' Тяните вдоль стены.',
    remove: 'Удалить',
  },
  wall: {
    title: 'Стена дома',
    fields: [
      { ...THICKNESS, max: 2000 },
      { key: 'length', label: 'Длина внутр.', min: 10, max: 1e6, step: 10, unit: 'мм' },
      { key: 'outer_length', label: 'Длина наружн.', min: 10, max: 1e6, step: 10, unit: 'мм' },
      { key: 'keep_angles', label: 'Сохранять углы (сдвигать соседние стены)', check: true, always: true },
      LOCK,
    ],
    info: () => 'Тяните — стена двигается параллельно себе. Двойной щелчок по стене — новый угол.',
    remove: 'Удалить дом',
  },
  vertex: {
    title: 'Угол дома',
    fields: COORDINATES,
    info: () => 'Тяните, чтобы переместить; угол притягивается к сетке и выравнивается по соседним.',
    remove: 'Удалить угол',
  },
  node: {
    title: 'Узел перегородок',
    fields: COORDINATES,
    info: (o) => `Перегородок в узле: ${o.count}. Тяните — узел двигается целиком. «Убрать узел» выпрямляет перегородку.`,
    remove: 'Убрать узел',
  },
  ruler: {
    title: 'Линейка',
    info: (o) => `Длина ${o.length} мм. Тяните за линию — целиком, за конец — с притягиванием к стенам и швам плитки.`,
    remove: 'Удалить',
  },
  joint: {
    title: 'Деформационный шов',
    fields: [{ key: 'gap', label: 'Ширина щели', min: 1, max: 100, step: 1, unit: 'мм' }],
    info: (o) => `Длина ${o.length} мм. Режет плитку: куски по обе стороны — отдельные подрезки.`,
    remove: 'Удалить',
  },
  threshold: {
    title: 'Стык полов в проёме',
    fields: [
      { key: 'right', label: 'У комнаты справа', min: 0, max: 10000, step: 5, unit: 'мм' },
      { key: 'skew', label: 'Наклон', min: -10000, max: 10000, step: 5, unit: 'мм' },
    ],
    info: (o) =>
      `Глубина проёма ${o.depth} мм: справа — «${o.right_room}», слева — ${o.left_room ? `«${o.left_room}»` : 'улица'}. ` +
      'Тяните поперёк стены; за конец у косяка — только этот конец (стык наискосок).',
    remove: '',
  },
  tee: {
    title: 'Примыкание',
    fields: COORDINATES,
    info: () => 'Конец перегородки на боку другой. Тяните вдоль неё; за её грань — станет узлом.',
    remove: '',
  },
};

/**
 * Панель: build — один раз; после каждого изменения — show(комната, каталоги),
 * showTool(инструмент, настройки, подсказка), showObject(предмет).
 * actions: {setField(key, value), chooseTile(), colorOnly(), rename(name),
 *           align(mode), setSetting(key, value), setObjectField(key, value), deleteObject(),
 *           setFloorSource(id), swapTile()}.
 */
export class Panel {
  constructor(root, actions) {
    this.root = root;
    this.actions = actions;
    this.inputs = new Map(); // ключ → элемент ввода
    this.build();
  }

  build() {
    this.empty = this.root.querySelector('#room-empty');
    this.room = this.root.querySelector('#room-settings');
    this.roomName = this.root.querySelector('#room-name');
    this.roomName.addEventListener('change', () => {
      const name = this.roomName.value.trim();
      if (name) this.actions.rename(name);
    });
    for (const button of this.root.querySelectorAll('[data-align]')) {
      button.addEventListener('click', () => this.actions.align(button.dataset.align));
    }
    this.toolSettings = this.root.querySelector('#tool-settings');
    this.settingInputs = {
      houseThickness: this.root.querySelector('#setting-house'),
      partitionThickness: this.root.querySelector('#setting-partition'),
      openingWidth: this.root.querySelector('#setting-opening'),
      grid: this.root.querySelector('#setting-grid'),
      jointGap: this.root.querySelector('#setting-joint'),
    };
    for (const [key, input] of Object.entries(this.settingInputs)) {
      input.addEventListener('change', () => {
        const value = Number(input.value);
        if (input.value !== '' && Number.isFinite(value)) {
          this.actions.setSetting(key, Math.min(Number(input.max), Math.max(Number(input.min), value)));
        }
      });
    }
    this.objectSettings = this.root.querySelector('#object-settings');
    this.objectFields = this.root.querySelector('#object-fields');
    this.objectFieldsKind = null; // для какого вида предмета построены поля
    this.root.querySelector('#object-delete').addEventListener('click', () => this.actions.deleteObject());
    this.shared = this.root.querySelector('#room-shared');
    this.roomInfo = this.root.querySelector('#room-info');
    this.floorSource = this.root.querySelector('#floor-source');
    this.floorSource.addEventListener('change', () => this.actions.setFloorSource(Number(this.floorSource.value)));
    this.root.querySelector('#swap-tile').addEventListener('click', () => this.actions.swapTile());
    this.enabled = this.root.querySelector('#tile-enabled');
    this.enabled.addEventListener('change', () => this.actions.setField('enabled', this.enabled.checked));
    this.textureName = this.root.querySelector('#texture-name');
    this.textureImage = this.root.querySelector('#texture-image');
    this.root.querySelector('#choose-tile').addEventListener('click', () => this.actions.chooseTile());
    this.colorOnly = this.root.querySelector('#color-only');
    this.colorOnly.addEventListener('click', () => this.actions.colorOnly());
    const fields = this.root.querySelector('#tile-fields');
    for (const section of SECTIONS) {
      const block = document.createElement('fieldset');
      const legend = document.createElement('legend');
      legend.textContent = section.title;
      block.append(legend);
      for (const field of section.fields) block.append(this.row(field));
      fields.append(block);
    }
  }

  row(field) {
    const row = document.createElement('label');
    row.className = field.check ? 'row check' : 'row';
    const caption = document.createElement('span');
    caption.className = 'caption';
    caption.textContent = field.label;
    let input;
    if (field.options) {
      input = document.createElement('select');
      field.options.forEach(([title], i) => input.add(new Option(title, String(i))));
      input.addEventListener('change', () => this.actions.setField(field.key, field.options[input.selectedIndex][1]));
    } else if (field.check) {
      input = document.createElement('input');
      input.type = 'checkbox';
      input.addEventListener('change', () => this.actions.setField(field.key, input.checked));
    } else if (field.color) {
      input = document.createElement('input');
      input.type = 'color';
      input.addEventListener('change', () => this.actions.setField(field.key, input.value));
    } else {
      input = document.createElement('input');
      input.type = 'number';
      input.inputMode = 'decimal';
      Object.assign(input, { min: field.min, max: field.max, step: field.step });
      input.addEventListener('change', () => {
        const value = Number(input.value);
        if (input.value !== '' && Number.isFinite(value)) {
          this.actions.setField(field.key, Math.min(field.max, Math.max(field.min, value)));
        }
      });
    }
    input.id = `tile-${field.key}`;
    this.inputs.set(field.key, { field, input });
    if (field.check) {
      row.append(input, caption);
    } else {
      const control = document.createElement('span');
      control.className = 'control';
      control.append(input);
      if (field.unit) {
        const unit = document.createElement('span');
        unit.className = 'unit';
        unit.textContent = field.unit;
        control.append(unit);
      }
      row.append(caption, control);
    }
    return row;
  }

  updateEmpty() {
    this.empty.hidden = !(this.room.hidden && this.toolSettings.hidden && this.objectSettings.hidden);
  }

  /** Инструмент рисования: его настройки и подсказка (у «Выбора» — скрыто). */
  showTool(tool, settings, hint) {
    this.toolSettings.hidden = tool === 'select';
    if (!this.toolSettings.hidden) {
      this.root.querySelector('#tool-title').textContent = TOOL_TITLES[tool];
      this.root.querySelector('#tool-hint').textContent = hint;
      for (const row of this.toolSettings.querySelectorAll('[data-tools]')) {
        row.hidden = !row.dataset.tools.split(' ').includes(tool);
      }
      for (const [key, input] of Object.entries(this.settingInputs)) {
        if (document.activeElement !== input) input.value = String(settings[key]);
      }
    }
    this.updateEmpty();
  }

  /** Выделенный предмет плана (tl_web_object) или null. */
  showObject(object) {
    this.objectSettings.hidden = !object;
    if (object) {
      const text = OBJECTS[object.kind];
      this.root.querySelector('#object-title').textContent = text.title;
      const info = text.info(object);
      this.root.querySelector('#object-info').textContent = object.locked
        ? `${info} Заблокировано в настольной программе — менять нельзя.`
        : info;
      this.showObjectFields(object, text.fields || []);
      const remove = this.root.querySelector('#object-delete');
      remove.hidden = !text.remove;
      remove.textContent = text.remove;
      remove.disabled = object.locked || (object.kind === 'vertex' && object.walls <= 3);
    }
    this.updateEmpty();
  }

  /** Поля предмета: строятся заново, только когда сменился его вид. */
  showObjectFields(object, fields) {
    if (this.objectFieldsKind !== object.kind) {
      this.objectFieldsKind = object.kind;
      this.objectFields.replaceChildren(...fields.map((field) => this.objectRow(field)));
    }
    for (const field of fields) {
      const input = this.objectFields.querySelector(`[data-key="${field.key}"]`);
      const value = object[field.key];
      input.closest('.row').hidden = value === undefined;
      if (field.check) input.checked = Boolean(value);
      else if (document.activeElement !== input && value !== undefined) input.value = String(Math.round(value * 10) / 10);
      input.disabled = object.locked && !field.always;
    }
  }

  objectRow(field) {
    const row = document.createElement('label');
    row.className = field.check ? 'row check' : 'row';
    if (field.check) {
      const box = document.createElement('input');
      box.type = 'checkbox';
      box.dataset.key = field.key;
      box.id = `object-${field.key}`;
      box.addEventListener('change', () => this.actions.setObjectField(field.key, box.checked));
      const caption = document.createElement('span');
      caption.className = 'caption';
      caption.textContent = field.label;
      row.append(box, caption);
      return row;
    }
    const caption = document.createElement('span');
    caption.className = 'caption';
    caption.textContent = field.label;
    const control = document.createElement('span');
    control.className = 'control';
    const input = document.createElement('input');
    input.type = 'number';
    input.inputMode = 'decimal';
    input.dataset.key = field.key;
    input.id = `object-${field.key}`;
    Object.assign(input, { min: field.min, max: field.max, step: field.step });
    input.addEventListener('change', () => {
      const value = Number(input.value);
      if (input.value !== '' && Number.isFinite(value)) {
        this.actions.setObjectField(field.key, Math.min(field.max, Math.max(field.min, value)));
      }
    });
    const unit = document.createElement('span');
    unit.className = 'unit';
    unit.textContent = field.unit;
    control.append(input, unit);
    row.append(caption, control);
    return row;
  }

  /** room — от ядра (tl_web_space_tile) или null; catalogs — для названия плитки. */
  show(room, catalogs) {
    this.room.hidden = !room;
    this.updateEmpty();
    if (!room) return;
    if (document.activeElement !== this.roomName) this.roomName.value = room.name;
    this.roomInfo.textContent = `${room.area.toFixed(2).replace('.', ',')} м², периметр ${room.perimeter} мм`;
    this.floorSource.replaceChildren(
      new Option('Своя раскладка', '0'),
      ...room.sources.map((source) => new Option(`Как в «${source.name}» (общая сетка)`, String(source.id))),
    );
    this.floorSource.value = String(room.source_id);
    if (this.floorSource.selectedIndex < 0) this.floorSource.value = '0';
    this.shared.hidden = !room.source;
    this.shared.textContent = room.source ? `Пол общий с комнатой «${room.source}»: настройки — её.` : '';
    const tile = room.tile;
    this.enabled.checked = tile.enabled;
    for (const { field, input } of this.inputs.values()) {
      const value = tile[field.key];
      if (document.activeElement === input) continue; // не мешать вводу
      if (field.options) {
        input.selectedIndex = nearestIndex(field.options, value);
      } else if (field.check) {
        input.checked = value;
      } else if (field.color) {
        input.value = value;
      } else {
        input.value = String(Math.round(value * 100) / 100);
      }
    }
    const found = catalogs.find(tile.texture);
    this.colorOnly.hidden = !tile.texture;
    this.textureImage.hidden = !found;
    if (found) this.textureImage.src = catalogs.imageUrl(found.catalog, found.tile);
    this.textureName.textContent = !tile.texture
      ? 'только цвет'
      : found
        ? found.tile.name
        : `нет в каталогах: ${tile.texture}`;
  }
}

function nearestIndex(options, value) {
  let best = 0;
  options.forEach(([, option], i) => {
    if (Math.abs(option - value) < Math.abs(options[best][1] - value)) best = i;
  });
  return best;
}

// ---- расчёт -------------------------------------------------------------------

const integer = new Intl.NumberFormat('ru-RU');
const decimal = new Intl.NumberFormat('ru-RU', { maximumFractionDigits: 2 });

/** Сводка к покупке (tl_web_summary) — в элемент root. */
export function showSummary(root, summary, catalogs) {
  root.replaceChildren();
  if (!summary.types.length) {
    root.append(paragraph('Ни в одной комнате нет плитки.'));
    return;
  }
  for (const type of summary.types) {
    const item = document.createElement('div');
    item.className = 'summary-type';
    const swatch = document.createElement('span');
    swatch.className = 'swatch';
    swatch.style.background = type.tile.color;
    const found = catalogs.find(type.tile.texture);
    if (found) swatch.style.backgroundImage = `url("${catalogs.imageUrl(found.catalog, found.tile)}")`;
    const title = document.createElement('div');
    title.className = 'summary-title';
    title.textContent = found ? `${found.tile.name}, ${type.name}` : type.name;
    const buy = document.createElement('div');
    buy.className = 'summary-buy';
    buy.textContent = `${integer.format(type.to_buy)} шт. к покупке`;
    const details = [
      `нужно ${integer.format(type.count)} шт. (целых ${integer.format(type.whole)}) + запас ${type.tile.reserve}%`,
      `${decimal.format(type.area)} м² · ${type.rooms.join(', ')}`,
    ];
    if (type.shared_saving + type.offcut_saving > 0) {
      details.push(`сэкономлено ${integer.format(type.shared_saving + type.offcut_saving)} шт. на подрезках`);
    }
    const text = document.createElement('div');
    text.className = 'summary-details';
    text.textContent = details.join('\n');
    const body = document.createElement('div');
    body.append(title, buy, text);
    item.append(swatch, body);
    root.append(item);
  }
  const packs = (list, unit, what) => list.map((p) => `${integer.format(p.count)} ${what} по ${decimal.format(p.size)} ${unit}`).join(', ');
  if (summary.glue.total_kg > 0) {
    root.append(paragraph(`Клей: ${decimal.format(summary.glue.total_kg)} кг — ${packs(summary.glue.packs, 'кг', 'меш.')}`));
  }
  if (summary.clips.total > 0) {
    root.append(paragraph(`СВП: ${integer.format(summary.clips.total)} шт. — ${packs(summary.clips.packs, 'шт.', 'упак.')}`));
  }
  root.append(paragraph(`Площадь пола: ${decimal.format(summary.floor_area)} м², под плитку: ${decimal.format(summary.tiled_area)} м²`));
}

function paragraph(text) {
  const p = document.createElement('p');
  p.textContent = text;
  return p;
}
