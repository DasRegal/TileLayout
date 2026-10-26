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

/**
 * Панель: build — один раз; show(roomTile, catalogs) — после каждого изменения.
 * actions: {setField(key, value), chooseTile(), colorOnly()}.
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
    this.shared = this.root.querySelector('#room-shared');
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

  /** room — от ядра (tl_web_space_tile) или null; catalogs — для названия плитки. */
  show(room, catalogs) {
    this.empty.hidden = Boolean(room);
    this.room.hidden = !room;
    if (!room) return;
    this.roomName.textContent = room.name;
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
