// Панель справа: плитка выбранной комнаты и расчёт к покупке. Поля — по
// таблице FIELDS: ключ как в файле проекта, подпись и пределы — как на панели
// настольной программы.

const ROW_SHIFTS = [
  ['Без сдвига', 0],
  ['Вразбежку 1/2', 0.5],
  ['Вразбежку 1/3', 1 / 3],
  ['Вразбежку 1/4', 0.25],
];

/* Рисунки укладки (src/core/tl_pattern.c): название и id в файле проекта. */
const PATTERNS = [
  ['Прямая', 'straight'],
  ['Шахматка', 'checker'],
  ['Ёлочка', 'herringbone'],
  ['Модульная: 2 формата', 'hopscotch'],
  ['Модульная: 3 формата', 'modular3'],
  ['Версаль: 4 формата', 'versailles'],
  ['Французская ёлочка (шеврон)', 'chevron'],
  ['Со вставкой (кабошон)', 'insert'],
  ['Корзинка', 'basket'],
];
const MODULAR = ['hopscotch', 'modular3', 'versailles'];
const patternOf = (tile) => (PATTERNS.some(([, id]) => id === tile.pattern) ? tile.pattern : 'straight');

/** Что значат поля плитки у рисунка (под выбором рисунка). */
function patternNote(room) {
  const pattern = patternOf(room.tile);
  if (MODULAR.includes(pattern)) return `Длина и ширина — клетка модуля. Форматы: ${room.formats.join(', ')}.`;
  if (pattern === 'herringbone') return 'Длинная сторона — вдоль ряда. Ёлочка по диагонали комнаты — поворот 45°.';
  if (pattern === 'checker') return 'Плитки двух цветов через одну: вторая — отдельная позиция к покупке.';
  if (pattern === 'chevron') return 'Длина — по длинной стороне плитки-параллелограмма, торцы срезаны под углом шеврона.';
  if (pattern === 'insert') return 'Углы плитки срезаны под квадратную вставку (кабошон) — её размер и цвет ниже.';
  if (pattern === 'basket') return 'Квадрат из нескольких плиток вдоль, рядом — поперёк. Ровно, если длина = n × ширина + швы.';
  return '';
}

/* Зоны пола (src/core/tl_zone.h): у зоны своя плитка, рисунок и цена; клей и затирка — от комнаты. */
const ZONES = [
  {
    key: 'border',
    title: 'Бордюр вдоль стен',
    check: 'Бордюр по периметру комнаты',
    fields: [{ key: 'width', label: 'Ширина полосы', min: 10, max: 5000, step: 10, unit: 'мм' }],
  },
  {
    key: 'carpet',
    title: 'Ковёр в центре',
    check: 'Ковёр другой плиткой в центре',
    fields: [
      { key: 'width', label: 'Размер по X', min: 10, max: 50000, step: 10, unit: 'мм' },
      { key: 'height', label: 'Размер по Y', min: 10, max: 50000, step: 10, unit: 'мм' },
      { key: 'dx', label: 'Сдвиг X от центра', min: -50000, max: 50000, step: 10, unit: 'мм' },
      { key: 'dy', label: 'Сдвиг Y от центра', min: -50000, max: 50000, step: 10, unit: 'мм' },
    ],
  },
];
/* Полосы облицовки стен (src/core/tl_wall.h) — как зоны: своя плитка; снизу вверх — низ, фриз, стены. */
const WALL_BANDS = [
  {
    key: 'wall_lower',
    title: 'Низ стен',
    check: 'Низ стен другой плиткой',
    fit: 'Целыми рядами',
    fields: [{ key: 'width', label: 'Высота низа', min: 10, max: 10000, step: 10, unit: 'мм' }],
  },
  {
    key: 'wall_frieze',
    title: 'Фриз',
    check: 'Фриз — узкая полоса',
    noFit: true, // один ряд: высота фриза — высота его плитки
    fields: [
      { key: 'h', label: 'Высота фриза', min: 5, max: 2000, step: 5, unit: 'мм', tile: true },
      { key: 'dy', label: 'От пола (без низа)', min: 0, max: 10000, step: 10, unit: 'мм' },
    ],
  },
  { key: 'wall_main', title: 'Плитка стен', fields: [] },
];
const WALL_FIELDS = [
  { key: 'room_height', label: 'Высота помещения', min: 500, max: 20000, step: 10, unit: 'мм' },
  { key: 'enabled', label: 'Облицовка стен плиткой', check: true },
  { key: 'height', label: 'Облицовка до (0 — до потолка)', min: 0, max: 20000, step: 10, unit: 'мм' },
];

/* Участок пола — выделенная часть комнаты между линиями раздела (room.region — её номер). */
const AREA = { key: 'area', title: 'Участок пола' };
const COVERINGS = [['Как в комнате', 'room'], ['Своя плитка', 'own'], ['Без плитки', 'none']];

/** Данные зоны комнаты (tl_web_space_tile): зона пола, полоса стен ('wall_lower'…) или участок ('area'). */
export const zoneData = (room, key) => {
  if (key === 'area') {
    const region = room.regions[room.region];
    return { enabled: region?.covering === 'own', tile: region?.tile || room.tile };
  }
  return key.startsWith('wall_') ? room.walls.bands[key.slice(5)] : room.zones[key];
};
/** Зона кладётся: полоса стен — при включённой облицовке. */
const zoneActive = (room, key) => (key.startsWith('wall_') ? room.walls.enabled && zoneData(room, key).enabled : zoneData(room, key).enabled);

const ZONE_TILE_FIELDS = [
  { key: 'pattern', label: 'Укладка', options: PATTERNS, tile: true },
  { key: 'shift', label: 'Сдвиг рядов', options: ROW_SHIFTS, tile: true },
  { key: 'w', label: 'Длина плитки', min: 10, max: 5000, step: 10, unit: 'мм', tile: true },
  { key: 'h', label: 'Ширина плитки', min: 10, max: 5000, step: 10, unit: 'мм', tile: true },
  { key: 'real_w', label: 'Факт. длина (калибр)', min: 0, max: 5000, step: 0.5, unit: 'мм', tile: true },
  { key: 'real_h', label: 'Факт. ширина (калибр)', min: 0, max: 5000, step: 0.5, unit: 'мм', tile: true },
  { key: 'gap', label: 'Шов', min: 0, max: 50, step: 0.5, unit: 'мм', tile: true },
  { key: 'angle', label: 'Поворот', min: -180, max: 180, step: 5, unit: '°', tile: true },
  { key: 'off_x', label: 'Смещение X', min: -100000, max: 100000, step: 5, unit: 'мм', tile: true },
  { key: 'off_y', label: 'Смещение Y', min: -100000, max: 100000, step: 5, unit: 'мм', tile: true },
  { key: 'color', label: 'Цвет', color: true, tile: true },
  { key: 'color2', label: 'Цвет второй плитки', color: true, tile: true },
  { key: 'price_tile', label: 'Цена плитки', min: 0, max: 1e6, step: 50, unit: '₽/м²', tile: true },
  { key: 'price_work', label: 'Цена укладки', min: 0, max: 1e6, step: 50, unit: '₽/м²', tile: true },
];

const SECTIONS = [
  {
    title: 'Плитка',
    fields: [
      { key: 'pattern', label: 'Укладка', options: PATTERNS, note: patternNote },
      { key: 'w', label: 'Длина плитки', min: 10, max: 5000, step: 10, unit: 'мм' },
      { key: 'h', label: 'Ширина плитки', min: 10, max: 5000, step: 10, unit: 'мм' },
      { key: 'real_w', label: 'Факт. длина (калибр)', min: 0, max: 5000, step: 0.5, unit: 'мм',
        note: () => 'Калибр: настоящий размер (598 у 600). Кладут по нему, покупают — по номинальному; 0 — как номинальный.' },
      { key: 'real_h', label: 'Факт. ширина (калибр)', min: 0, max: 5000, step: 0.5, unit: 'мм' },
      { key: 'gap', label: 'Шов (зазор)', min: 0, max: 50, step: 0.5, unit: 'мм' },
      { key: 'pattern_angle', label: 'Угол шеврона', min: 15, max: 75, step: 5, unit: '°', when: (tile) => patternOf(tile) === 'chevron' },
      { key: 'insert', label: 'Вставка (кабошон)', min: 10, max: 500, step: 5, unit: 'мм', when: (tile) => patternOf(tile) === 'insert' },
      { key: 'shift', label: 'Сдвиг рядов', options: ROW_SHIFTS, when: (tile) => patternOf(tile) === 'straight' },
      { key: 'angle', label: 'Поворот', min: -180, max: 180, step: 5, unit: '°' },
      { key: 'off_x', label: 'Смещение X', min: -100000, max: 100000, step: 5, unit: 'мм' },
      { key: 'off_y', label: 'Смещение Y', min: -100000, max: 100000, step: 5, unit: 'мм' },
      { key: 'wall_gap', label: 'Отступ от стены', min: 0, max: 100, step: 1, unit: 'мм' },
      { key: 'min_cut', label: 'Мин. ширина подрезки', min: 0, max: 1000, step: 5, unit: 'мм' },
      { key: 'reuse', label: 'Использовать остатки подрезок', check: true },
      { key: 'cut_allow', label: 'Запас на рез', min: 0, max: 20, step: 0.5, unit: 'мм' },
      { key: 'reserve', label: 'Запас', min: 0, max: 100, step: 1, unit: '%' },
      { key: 'color', label: 'Цвет', color: true },
      { key: 'color2', label: 'Цвет второй плитки', color: true, when: (tile) => ['checker', 'insert'].includes(patternOf(tile)) },
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
      { key: 'crosses', label: 'Крестики вместо СВП', check: true },
      { key: 'crosses_pack', label: 'Крестиков в упаковке', min: 1, max: 100000, step: 50, unit: 'шт.' },
    ],
  },
  {
    title: 'Затирка и грунт',
    fields: [
      { key: 'thickness', label: 'Толщина плитки (глубина шва)', min: 1, max: 50, step: 0.5, unit: 'мм' },
      { key: 'grout_density', label: 'Плотность затирки', min: 0.5, max: 3, step: 0.05, unit: 'кг/дм³' },
      { key: 'grout_pack', label: 'Затирка в упаковке', min: 0.1, max: 100, step: 0.5, unit: 'кг' },
      { key: 'primer_rate', label: 'Расход грунта', min: 0, max: 2, step: 0.05, unit: 'л/м²' },
      { key: 'primer_can', label: 'Грунт в канистре', min: 0.5, max: 100, step: 0.5, unit: 'л' },
    ],
  },
  {
    title: 'Гидроизоляция',
    fields: [
      { key: 'waterproof', label: 'Гидроизоляция пола (санузел, душ)', check: true },
      { key: 'waterproof_height', label: 'Заход на стены', min: 0, max: 3000, step: 50, unit: 'мм' },
      { key: 'waterproof_rate', label: 'Расход на слой', min: 0.05, max: 10, step: 0.05, unit: 'кг/м²' },
      { key: 'waterproof_layers', label: 'Слоёв', min: 1, max: 5, step: 1, unit: '' },
      { key: 'waterproof_pack', label: 'В упаковке', min: 0.5, max: 100, step: 0.5, unit: 'кг' },
    ],
  },
  {
    title: 'Профили',
    fields: [
      { key: 'threshold_profile', label: 'Порог-профиль в дверях', check: true },
      { key: 'corner_height', label: 'Уголки на наружные углы, высота (0 — не нужны)', min: 0, max: 5000, step: 50, unit: 'мм' },
      { key: 'profile_length', label: 'Длина планки', min: 500, max: 6000, step: 100, unit: 'мм' },
    ],
  },
  {
    title: 'Цены для сметы',
    hint: '0 — позиция без цены, в смету не идёт.',
    fields: [
      { key: 'price_tile', label: 'Плитка', min: 0, max: 1e6, step: 50, unit: '₽' },
      { key: 'price_tile_unit', label: 'Цена плитки — за', options: [['м²', 0], ['штуку', 1], ['упаковку', 2]] },
      { key: 'tile_pack', label: 'Плиток в упаковке (0 — поштучно)', min: 0, max: 1000, step: 1, unit: 'шт.' },
      { key: 'price_glue', label: 'Клей', min: 0, max: 1e6, step: 10, unit: '₽/мешок' },
      { key: 'price_svp', label: 'СВП', min: 0, max: 1e6, step: 10, unit: '₽/уп.' },
      { key: 'price_grout', label: 'Затирка', min: 0, max: 1e6, step: 10, unit: '₽/уп.' },
      { key: 'price_primer', label: 'Грунт', min: 0, max: 1e6, step: 10, unit: '₽/кан.' },
      { key: 'price_cross', label: 'Крестики', min: 0, max: 1e6, step: 10, unit: '₽/уп.' },
      { key: 'price_waterproof', label: 'Гидроизоляция', min: 0, max: 1e6, step: 10, unit: '₽/уп.' },
      { key: 'price_tape', label: 'Лента гидроизоляции', min: 0, max: 1e6, step: 10, unit: '₽/м' },
      { key: 'price_threshold', label: 'Порог (планка)', min: 0, max: 1e6, step: 10, unit: '₽/шт.' },
      { key: 'price_corner', label: 'Уголок (планка)', min: 0, max: 1e6, step: 10, unit: '₽/шт.' },
      { key: 'price_work', label: 'Укладка', min: 0, max: 1e6, step: 50, unit: '₽/м²' },
      { key: 'price_cut', label: 'Подрезка', min: 0, max: 1e6, step: 10, unit: '₽/пог. м' },
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
  pipe: 'Труба',
  divider: 'Раздел покрытия',
  fixture: 'Предмет',
  drain: 'Трап',
  underlay_calibrate: 'Калибровка подложки',
  underlay_move: 'Сдвиг подложки',
};

/* Предметы в комнате (src/core/tl_fixture.h): название и вид в файле проекта. */
const FIXTURE_KINDS = [['Ванна', 'bath'], ['Душевой поддон', 'tray'], ['Инсталляция', 'installation'], ['Тумба', 'cabinet'], ['Кухня', 'kitchen'], ['Лестница', 'stairs']];

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
      { key: 'name', label: 'Имя', text: true },
      { key: 'opening_kind', label: 'Вид', options: [['Дверь', 'door'], ['Окно', 'window'], ['Арка', 'arch']] },
      { key: 'width', label: 'Ширина', min: 100, max: 10000, step: 10, unit: 'мм' },
      { key: 'height', label: 'Высота', min: 10, max: 20000, step: 10, unit: 'мм' },
      { key: 'sill', label: 'От пола (подоконник)', min: 0, max: 20000, step: 10, unit: 'мм' },
      { key: 'reveal', label: 'Откосы плиткой (глубина)', min: 0, max: 1000, step: 10, unit: 'мм' },
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
    fields: [{ key: 'name', label: 'Имя', text: true, always: true }],
    info: (o) => `Длина ${o.length} мм. Тяните за линию — целиком, за конец — с притягиванием к стенам и швам плитки.`,
    remove: 'Удалить',
  },
  joint: {
    title: (o) => (o.divider ? 'Линия раздела' : 'Деформационный шов'),
    fields: [{ key: 'gap', label: 'Ширина щели', min: 0.5, max: 100, step: 0.5, unit: 'мм' }],
    info: (o) => (o.divider
      ? `Длина ${o.length} мм. Делит пол комнаты на части: щёлкните часть, чтобы задать ей свою плитку или «без плитки». Концы на других линиях держатся за них.`
      : `Длина ${o.length} мм. Режет плитку: куски по обе стороны — отдельные подрезки.`),
    remove: 'Удалить',
  },
  pipe: {
    title: 'Труба (отверстие в плитке)',
    fields: [
      { key: 'diameter', label: 'Диаметр коронки', min: 5, max: 500, step: 1, unit: 'мм' },
      { key: 'x', label: 'Центр X', min: -1e7, max: 1e7, step: 10, unit: 'мм' },
      { key: 'y', label: 'Центр Y', min: -1e7, max: 1e7, step: 10, unit: 'мм' },
    ],
    info: () => 'Плитку с отверстием видно в чертежах подрезок: где сверлить и какой коронкой. Тяните, чтобы сдвинуть.',
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
  room_wall: {
    title: (o) => `Стена ${o.number}`,
    fields: [
      { key: 'offset_x', label: 'Сдвиг плитки →', min: -100000, max: 100000, step: 10, unit: 'мм' },
      { key: 'offset_y', label: 'Сдвиг плитки ↑', min: -100000, max: 100000, step: 10, unit: 'мм' },
    ],
    buttons: [['Швы как у пола', 'floor'], ['От левого угла', 'reset']],
    info: (o) => `Длина ${o.length} мм. Ctrl + перетаскивание стены — сдвиг плитки мышью. Под стеной — пол у неё.`,
    remove: '',
  },
  wall_pipe: {
    title: 'Отверстие на стене',
    fields: [
      { key: 'diameter', label: 'Диаметр коронки', min: 5, max: 500, step: 1, unit: 'мм' },
      { key: 'x', label: 'От левого угла', min: 0, max: 100000, step: 10, unit: 'мм' },
      { key: 'h', label: 'От пола', min: 0, max: 20000, step: 10, unit: 'мм' },
    ],
    info: () => 'Розетка, смеситель, вывод трубы: плитку с отверстием видно в чертежах подрезок. Тяните, чтобы сдвинуть.',
    remove: 'Удалить',
  },
  panno: {
    title: 'Панно',
    fields: [
      { key: 'columns', label: 'Плиток по ширине', min: 1, max: 64, step: 1, unit: 'шт.' },
      { key: 'rows', label: 'Плиток по высоте', min: 1, max: 64, step: 1, unit: 'шт.' },
      { key: 'w', label: 'Плитка: ширина', min: 10, max: 5000, step: 10, unit: 'мм' },
      { key: 'h', label: 'Плитка: высота', min: 10, max: 5000, step: 10, unit: 'мм' },
      { key: 'gap', label: 'Шов', min: 0, max: 50, step: 0.5, unit: 'мм' },
      { key: 'x', label: 'X: от левого угла / от центра', min: -100000, max: 100000, step: 10, unit: 'мм' },
      { key: 'y', label: 'Y: низ от пола / от центра', min: -100000, max: 100000, step: 10, unit: 'мм' },
      { key: 'color', label: 'Цвет', color: true },
      { key: 'price', label: 'Цена комплекта', min: 0, max: 1e7, step: 100, unit: '₽' },
    ],
    buttons: [['Картинка…', 'panno_texture'], ['Только цвет', 'panno_color']],
    info: (o) => `${o.width} × ${o.height} мм, ${o.wall < 0 ? 'на полу: сдвиг от центра комнаты' : `на стене ${o.wall + 1}: от левого угла и от пола`}. ` +
      `Картинка — ${o.texture ? 'из каталога' : 'нет, цвет'}. Кладётся целиком, плитка вокруг обходит его со швом. Тяните мышью.`,
    remove: 'Удалить',
  },
  drain: {
    title: (o) => o.name,
    fields: [
      { key: 'drain_kind', label: 'Вид', options: [['Трап', 'point'], ['Линейный трап', 'linear']] },
      { key: 'size', label: 'Решётка / ширина', min: 10, max: 1000, step: 5, unit: 'мм' },
      { key: 'length', label: 'Длина лотка', min: 10, max: 10000, step: 50, unit: 'мм' },
      { key: 'angle', label: 'Поворот', min: -360, max: 360, step: 5, unit: '°' },
      { key: 'slope', label: 'Уклон', min: 0, max: 10, step: 0.1, unit: '%' },
      { key: 'envelope', label: 'Конверт: резать плитку к углам', check: true },
    ],
    info: (o) => (o.drop !== undefined ? `Перепад до дальнего угла — ${o.drop} мм. ` : 'Трап не в комнате. ') +
      (o.drain_kind === 'linear' ? 'Пол — одним уклоном к лотку, без разрезов.' : 'Тяните мышью; плитка режется по линиям к углам.'),
    remove: 'Удалить',
  },
  fixture: {
    title: (o) => o.name,
    fields: [
      { key: 'fixture_kind', label: 'Что это', options: FIXTURE_KINDS },
      { key: 'w', label: 'Длина', min: 10, max: 20000, step: 10, unit: 'мм' },
      { key: 'd', label: 'Глубина', min: 10, max: 20000, step: 10, unit: 'мм' },
      { key: 'h', label: 'Высота', min: 0, max: 5000, step: 10, unit: 'мм' },
      { key: 'angle', label: 'Поворот', min: -360, max: 360, step: 5, unit: '°' },
      { key: 'steps', label: 'Ступеней', min: 1, max: 50, step: 1, unit: 'шт' },
      { key: 'under', label: 'Плитка под предметом', check: true },
      { key: 'behind', label: 'Плитка за предметом на стенах', check: true },
    ],
    buttons: [['Повернуть на 90°', 'fixture_rotate']],
    info: (o) => o.steps !== undefined
      ? 'Подъём — от переда к спинке. Плитка ступеней (проступи и подступенки) — на развёртке комнаты, считается как пол.'
      : 'Тяните мышью: у стены предмет встаёт к ней спинкой. Без плитки под ним пол его обходит, без плитки за ним стена не облицовывается до его высоты.',
    remove: 'Удалить',
  },
  wall_ruler: {
    title: 'Линейка на стене',
    fields: [],
    info: (o) => `Длина ${o.length} мм. Тяните за линию — целиком, за конец — с притягиванием к швам и краям.`,
    remove: 'Удалить',
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
 *           setFloorSource(id), swapTile(), toggleMulti(), setZoneField(zone, key, value), chooseZoneTile(zone),
 *           setWalls(key, value), openWalls()}.
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
    this.doorChoice = this.root.querySelector('#align-door');
    this.rulerChoice = this.root.querySelector('#align-ruler');
    for (const button of this.root.querySelectorAll('[data-align]')) {
      const mode = button.dataset.align;
      /* от двери и по линейке — с номером выбранного предмета в проекте: "door:3" */
      const choice = mode === 'door' ? this.doorChoice : mode.startsWith('ruler') ? this.rulerChoice : null;
      button.addEventListener('click', () => this.actions.align(choice ? `${mode}:${choice.value}` : mode));
    }
    this.toolSettings = this.root.querySelector('#tool-settings');
    this.settingInputs = {
      houseThickness: this.root.querySelector('#setting-house'),
      partitionThickness: this.root.querySelector('#setting-partition'),
      openingWidth: this.root.querySelector('#setting-opening'),
      grid: this.root.querySelector('#setting-grid'),
      jointGap: this.root.querySelector('#setting-joint'),
      pipeDiameter: this.root.querySelector('#setting-pipe'),
      dividerGap: this.root.querySelector('#setting-divider'),
      fixtureKind: this.root.querySelector('#setting-fixture'),
      drainKind: this.root.querySelector('#setting-drain'),
      calibration: this.root.querySelector('#setting-calibration'),
    };
    for (const [key, input] of Object.entries(this.settingInputs)) {
      input.addEventListener('change', () => {
        if (input.tagName === 'SELECT') { // вид нового предмета
          this.actions.setSetting(key, input.value);
          return;
        }
        const value = Number(input.value);
        if (input.value !== '' && Number.isFinite(value)) {
          this.actions.setSetting(key, Math.min(Number(input.max), Math.max(Number(input.min), value)));
        }
      });
    }
    this.underlaySettings = this.root.querySelector('#underlay-settings');
    for (const button of this.root.querySelectorAll('[data-underlay]')) {
      button.addEventListener('click', () => this.actions.underlay(button.dataset.underlay));
    }
    const underlayField = (id, key, read) => {
      const input = this.root.querySelector(id);
      input.addEventListener('change', () => {
        const value = read(input);
        if (value !== null) this.actions.underlay('set', { [key]: value });
      });
    };
    const number = (scale) => (input) => (input.value !== '' && Number(input.value) > 0 ? Number(input.value) * scale : null);
    underlayField('#underlay-visible', 'visible', (input) => input.checked);
    underlayField('#underlay-opacity', 'opacity', (input) => {
      const value = number(1)(input);
      return value === null ? null : Math.min(100, Math.max(5, value)) / 100;
    });
    underlayField('#underlay-scale', 'scale', number(1));
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
    this.roomsTitle = this.root.querySelector('#rooms-title');
    this.multi = this.root.querySelector('#multi-rooms');
    this.multi.addEventListener('click', () => this.actions.toggleMulti());
    this.floorRow = this.root.querySelector('#floor-row');
    this.align = this.root.querySelector('.align');
    this.colorOnly = this.root.querySelector('#color-only');
    this.colorOnly.addEventListener('click', () => this.actions.colorOnly());
    const fields = this.root.querySelector('#tile-fields');
    this.floorBlocks = []; // разделы пола — на вкладке стен скрыты
    this.tileEnabledRow = this.enabled.closest('.row');
    this.texture = this.root.querySelector('.texture');
    for (const section of SECTIONS) {
      const block = document.createElement('fieldset');
      const legend = document.createElement('legend');
      legend.textContent = section.title;
      block.append(legend);
      if (section.hint) block.append(hint(section.hint));
      for (const field of section.fields) {
        block.append(this.row(field));
        if (field.note) block.append((this.inputs.get(field.key).note = hint('')));
      }
      fields.append(block);
      this.floorBlocks.push(block);
    }
    this.zoneInputs = []; // {zone, field, input}
    for (const zone of ZONES) this.floorBlocks.push(fields.appendChild(this.zoneBlock(zone)));
    this.floorBlocks.push(fields.appendChild(this.pannoBlock('Добавить панно на пол')));
    this.areaBlock = fields.insertBefore(this.buildAreaBlock(), fields.firstChild);
    fields.append(this.wallsBlock());
    this.bandBlocks = WALL_BANDS.map((zone) => fields.appendChild(this.zoneBlock(zone)));
    this.bandBlocks.push(fields.appendChild(this.pannoBlock('Добавить панно на стену')));
  }

  /** Участок пола: покрытие выделенной части комнаты, название и плитка (как у зон). */
  buildAreaBlock() {
    const block = document.createElement('fieldset');
    const legend = document.createElement('legend');
    legend.textContent = AREA.title;
    this.areaInfo = hint('');
    const covering = { key: 'covering', label: 'Покрытие', options: COVERINGS };
    const coveringRow = this.row(covering, (value) => this.actions.setAreaCovering(value), 'area-');
    this.areaCovering = coveringRow.querySelector('select');
    const nameRow = document.createElement('label');
    nameRow.className = 'row';
    nameRow.innerHTML = '<span class="caption">Название</span><span class="control"><input type="text" id="area-name" maxlength="60"></span>';
    this.areaName = nameRow.querySelector('input');
    this.areaName.addEventListener('change', () => {
      if (this.areaName.value.trim()) this.actions.renameArea(this.areaName.value.trim());
    });
    block.append(legend, this.areaInfo, coveringRow, nameRow);
    const size = hint('');
    const texture = document.createElement('div');
    texture.className = 'button-row zone-texture';
    const name = document.createElement('span');
    name.className = 'texture-name';
    const choose = document.createElement('button');
    choose.type = 'button';
    choose.textContent = 'Рисунок…';
    choose.addEventListener('click', () => this.actions.chooseZoneTile('area'));
    const color = document.createElement('button');
    color.type = 'button';
    color.textContent = 'Только цвет';
    color.addEventListener('click', () => this.actions.setZoneField('area', 'texture', ''));
    texture.append(name, choose, color);
    block.append(size, texture);
    this.zoneExtras = { ...this.zoneExtras, area: { size, name, buttons: [choose, color] } };
    for (const field of ZONE_TILE_FIELDS) block.append(this.zoneRow(AREA, field));
    return block;
  }

  /** Панно — кнопка «Добавить» (на пол или на выделенную стену) и пояснение. */
  pannoBlock(label) {
    const block = document.createElement('fieldset');
    const legend = document.createElement('legend');
    legend.textContent = 'Панно';
    const add = document.createElement('button');
    add.type = 'button';
    add.textContent = label;
    add.addEventListener('click', () => this.actions.addPanno());
    const buttons = document.createElement('div');
    buttons.className = 'button-row';
    buttons.append(add);
    block.append(legend, hint('Картинка из нескольких плиток: кладётся целиком, плитка вокруг обходит его со швом; в смете — комплектом.'), buttons);
    return block;
  }

  /** Стены комнаты: высота помещения, облицовка и её высота, кнопка развёртки. */
  wallsBlock() {
    const block = document.createElement('fieldset');
    const legend = document.createElement('legend');
    legend.textContent = 'Стены комнаты';
    block.append(legend);
    this.wallInputs = WALL_FIELDS.map((field) => {
      const row = this.row(field, (value) => this.actions.setWalls(field.key, value), 'walls-');
      block.append(row);
      return { field, input: row.querySelector('input') };
    });
    const open = document.createElement('button');
    open.type = 'button';
    open.dataset.icon = 'walls';
    open.textContent = 'Развёртка стен';
    open.title = 'Вкладка комнаты: стены в ряд, как видно из комнаты (или двойной щелчок по комнате)';
    open.addEventListener('click', () => this.actions.openWalls());
    const floor = document.createElement('button');
    floor.type = 'button';
    floor.textContent = 'Швы всех стен как у пола';
    floor.title = 'Сдвинуть плитку каждой стены так, чтобы шов нижнего ряда продолжал шов пола у стены';
    floor.addEventListener('click', () => this.actions.wallAction('floor_all'));
    const buttons = document.createElement('div');
    buttons.className = 'button-row';
    buttons.append(open, floor);
    block.append(buttons);
    this.wallsHint = hint('Щелчок по стене — её плитка; Ctrl + перетаскивание — сдвиг плитки. «Линейка» и «Труба» ставятся прямо на стену. Под стенами — пол у них.');
    block.append(this.wallsHint);
    this.wallsOpen = open;
    this.wallsFloor = floor;
    return block;
  }

  /** Раздел зоны: флажок, размеры и плитка зоны. */
  zoneBlock(zone) {
    const block = document.createElement('fieldset');
    const legend = document.createElement('legend');
    legend.textContent = zone.title;
    block.append(legend);
    const enabled = zone.check && { key: 'enabled', label: zone.check, check: true };
    const fit = zone.check && !zone.noFit && { key: 'fit', label: zone.fit || 'По целой плитке', check: true };
    for (const field of [enabled, ...zone.fields, fit].filter(Boolean)) block.append(this.zoneRow(zone, field));
    const size = hint('');
    block.append(size);
    const texture = document.createElement('div');
    texture.className = 'button-row zone-texture';
    const name = document.createElement('span');
    name.className = 'texture-name';
    const choose = document.createElement('button');
    choose.type = 'button';
    choose.textContent = 'Рисунок…';
    choose.title = 'Плитка зоны из каталога: размер и рисунок';
    choose.addEventListener('click', () => this.actions.chooseZoneTile(zone.key));
    const color = document.createElement('button');
    color.type = 'button';
    color.textContent = 'Только цвет';
    color.addEventListener('click', () => this.actions.setZoneField(zone.key, 'texture', ''));
    texture.append(name, choose, color);
    block.append(texture);
    this.zoneExtras = { ...this.zoneExtras, [zone.key]: { size, name, buttons: [choose, color] } };
    for (const field of ZONE_TILE_FIELDS) {
      if (!(zone.noFit && field.key === 'h')) block.append(this.zoneRow(zone, field)); // у фриза — «Высота фриза»
    }
    return block;
  }

  zoneRow(zone, field) {
    const row = this.row(field, (value) => this.actions.setZoneField(zone.key, field.key, value), `zone-${zone.key}-`);
    this.zoneInputs.push({ zone: zone.key, field, input: row.querySelector('input, select') });
    return row;
  }

  /** Строка поля; set(value) — что делать с введённым (по умолчанию — поле плитки комнаты). */
  row(field, set = (value) => this.actions.setField(field.key, value), prefix = 'tile-') {
    const row = document.createElement('label');
    row.className = field.check ? 'row check' : 'row';
    const caption = document.createElement('span');
    caption.className = 'caption';
    caption.textContent = field.label;
    let input;
    if (field.options) {
      input = document.createElement('select');
      field.options.forEach(([title], i) => input.add(new Option(title, String(i))));
      const mixed = new Option('разные', 'mixed'); // у нескольких комнат значения разные
      mixed.hidden = true;
      input.add(mixed);
      input.addEventListener('change', () => set(field.options[input.selectedIndex][1]));
      if (field.key === 'pattern') input.title = 'Как лежит плитка. Ёлочка по диагонали — поворот 45°.';
    } else if (field.check) {
      input = document.createElement('input');
      input.type = 'checkbox';
      input.addEventListener('change', () => set(input.checked));
    } else if (field.color) {
      input = document.createElement('input');
      input.type = 'color';
      input.addEventListener('change', () => set(input.value));
    } else {
      input = document.createElement('input');
      input.type = 'number';
      input.inputMode = 'decimal';
      Object.assign(input, { min: field.min, max: field.max, step: field.step });
      input.addEventListener('change', () => {
        const value = Number(input.value);
        if (input.value !== '' && Number.isFinite(value)) set(Math.min(field.max, Math.max(field.min, value)));
      });
    }
    input.id = `${prefix}${field.key}`;
    if (prefix === 'tile-') this.inputs.set(field.key, { field, input, row });
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
    this.underlaySettings.hidden = this.empty.hidden;
  }

  /** Подложка (положение из сцены): без неё — кнопки загрузки, с ней — прозрачность, масштаб, калибровка. */
  showUnderlay(underlay) {
    const present = Boolean(underlay?.present);
    this.root.querySelector('#underlay-load').hidden = present;
    this.root.querySelector('#underlay-controls').hidden = !present;
    this.root.querySelector('#underlay-hint').textContent = !present
      ? 'Фото или DXF плана квартиры — под план, чтобы обвести стены.'
      : underlay.kind === 'dxf'
        ? 'DXF-план: стены притягиваются к его углам и линиям.'
        : 'Откалибруйте масштаб по известному размеру, затем обводите стены.';
    if (!present) return;
    const set = (id, value) => {
      const input = this.root.querySelector(id);
      if (document.activeElement !== input) input.value = String(value);
    };
    this.root.querySelector('#underlay-visible').checked = underlay.visible;
    set('#underlay-opacity', Math.round(underlay.opacity * 100));
    set('#underlay-scale', Math.round(underlay.scale * 1000) / 1000);
    const image = underlay.kind === 'image';
    this.root.querySelector('#underlay-scale-caption').textContent = image ? 'Пиксель фото' : 'Масштаб';
    this.root.querySelector('#underlay-scale-unit').textContent = image ? 'мм' : '×';
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

  objectTitle(object) {
    const title = OBJECTS[object.kind].title;
    return typeof title === 'function' ? title(object) : title;
  }

  /** Выделенный предмет плана (tl_web_object) или null. */
  showObject(object) {
    this.objectSettings.hidden = !object;
    if (object) {
      const text = OBJECTS[object.kind];
      this.root.querySelector('#object-title').textContent = this.objectTitle(object);
      const info = text.info(object);
      this.root.querySelector('#object-info').textContent = object.locked
        ? `${info} Заблокировано в настольной программе — менять нельзя.`
        : info;
      this.showObjectFields(object, text.fields || []);
      const remove = this.root.querySelector('#object-delete');
      remove.hidden = !text.remove;
      remove.querySelector('.label').textContent = text.remove;
      remove.disabled = object.locked || (object.kind === 'vertex' && object.walls <= 3);
    }
    this.updateEmpty();
  }

  /** Поля предмета: строятся заново, только когда сменился его вид. */
  showObjectFields(object, fields) {
    if (this.objectFieldsKind !== object.kind) {
      this.objectFieldsKind = object.kind;
      this.objectFields.replaceChildren(...fields.map((field) => this.objectRow(field)));
      const buttons = OBJECTS[object.kind].buttons;
      if (buttons) { // действия со стеной развёртки
        const row = document.createElement('div');
        row.className = 'button-row';
        for (const [label, action] of buttons) {
          const button = document.createElement('button');
          button.type = 'button';
          button.textContent = label;
          button.addEventListener('click', () => this.actions.wallAction(action));
          row.append(button);
        }
        this.objectFields.append(row);
      }
    }
    for (const field of fields) {
      const input = this.objectFields.querySelector(`[data-key="${field.key}"]`);
      const value = object[field.key];
      input.closest('.row').hidden = value === undefined;
      if (field.check) input.checked = Boolean(value);
      else if (field.options || field.color) input.value = value;
      else if (field.text) {
        if (document.activeElement !== input && value !== undefined) input.value = value;
      } else if (document.activeElement !== input && value !== undefined) input.value = String(Math.round(value * 10) / 10);
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
    if (field.color) { // цвет панно
      const color = document.createElement('input');
      color.type = 'color';
      color.dataset.key = field.key;
      color.id = `object-${field.key}`;
      color.addEventListener('change', () => this.actions.setObjectField(field.key, color.value));
      control.append(color);
      row.append(caption, control);
      return row;
    }
    if (field.options) { // вид проёма
      const select = document.createElement('select');
      select.dataset.key = field.key;
      select.id = `object-${field.key}`;
      for (const [title, value] of field.options) select.add(new Option(title, value));
      select.addEventListener('change', () => this.actions.setObjectField(field.key, select.value));
      control.append(select);
      row.append(caption, control);
      return row;
    }
    const input = document.createElement('input');
    input.dataset.key = field.key;
    input.id = `object-${field.key}`;
    if (field.text) { // имя линейки или проёма
      input.type = 'text';
      input.maxLength = 11;
      input.addEventListener('change', () => {
        if (input.value.trim()) this.actions.setObjectField(field.key, input.value.trim());
      });
      control.append(input);
      row.append(caption, control);
      return row;
    }
    input.type = 'number';
    input.inputMode = 'decimal';
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

  /**
   * rooms — выделенные комнаты от ядра (tl_web_space_tile), может быть пусто;
   * catalogs — для названия плитки; multi — включён выбор нескольких комнат.
   * У нескольких комнат поле с разными значениями пустое («разные»): введённое
   * получат все; смещение сетки и раскладка от угла — только у одной комнаты.
   */
  show(rooms, catalogs, multi, walls = false, region = -1) {
    const room = rooms[0];
    const several = rooms.length > 1;
    this.room.hidden = !room;
    this.updateEmpty();
    if (!room) return;
    room.region = region; // выделенная часть пола (участок)
    const part = room.regions[region];
    this.areaBlock.hidden = walls || several || !part;
    if (part) {
      this.areaInfo.textContent = `Часть ${region + 1} из ${room.regions.length}, ${String(part.area).replace('.', ',')} м². ` +
        'Части разделены линиями «Раздел» и деформационными швами.';
      showValue({ options: COVERINGS }, this.areaCovering, part.covering);
      this.areaName.disabled = part.covering === 'room';
      if (document.activeElement !== this.areaName) this.areaName.value = part.name;
    }
    /* на вкладке стен — только стены комнаты; на плане — пол и кнопка развёртки */
    for (const block of this.floorBlocks) block.hidden = walls;
    for (const block of this.bandBlocks) block.hidden = !walls;
    this.tileEnabledRow.hidden = walls;
    this.texture.hidden = walls;
    this.wallsOpen.hidden = walls;
    this.wallsFloor.hidden = !walls;
    this.wallsHint.hidden = !walls;
    this.multi.hidden = walls;
    this.multi.setAttribute('aria-pressed', String(multi));
    this.roomName.hidden = several;
    this.roomsTitle.hidden = !several;
    this.floorRow.hidden = several || walls;
    this.align.hidden = several || walls;
    if (several) {
      const area = rooms.reduce((sum, r) => sum + r.area, 0);
      this.roomsTitle.textContent = `Комнат: ${rooms.length}`;
      this.roomInfo.textContent =
        `${rooms.map((r) => `«${r.name}»`).join(', ')} — ${area.toFixed(2).replace('.', ',')} м². ` +
        'Изменения применяются ко всем.';
      this.shared.hidden = true;
      this.showTile(rooms, catalogs);
      return;
    }
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
    fillChoice(this.doorChoice, room.doors.map((door) => [door.name, door.index]));
    fillChoice(this.rulerChoice, room.rulers.map((ruler) => [`${ruler.name} (${ruler.length} мм)`, ruler.index]));
    this.showTile(rooms, catalogs);
  }

  /** Зоны первой выделенной комнаты; без флажка зоны её поля недоступны. */
  showZones(room, catalogs) {
    const zones = [...ZONES, ...WALL_BANDS, ...(room.regions[room.region] ? [AREA] : [])];
    for (const zone of zones) {
      const data = zoneData(room, zone.key);
      const extras = this.zoneExtras[zone.key];
      extras.size.textContent = zone.key === 'border'
        ? `Кладётся полосой ${data.laid_width} мм`
        : zone.key === 'carpet'
          ? `Кладётся ковром ${data.laid_width} × ${data.laid_height} мм`
          : zone.key === 'area' ? '' : spansText(data.spans);
      const found = data.tile.texture && catalogs.find(data.tile.texture);
      extras.name.textContent = !data.tile.texture ? 'только цвет' : found ? found.tile.name : data.tile.texture;
      for (const button of extras.buttons) button.disabled = !zoneActive(room, zone.key);
    }
    for (const { zone, field, input } of this.zoneInputs) {
      if (zone === 'area' && !room.regions[room.region]) continue;
      const data = zoneData(room, zone);
      const value = !field.tile ? data[field.key] : data.tile[field.key];
      showValue(field, input, field.key === 'pattern' ? data.tile.pattern || 'straight' : value);
      const wall = zone.startsWith('wall_');
      input.disabled = field.key === 'enabled' ? wall && !room.walls.enabled : !zoneActive(room, zone);
    }
    for (const { field, input } of this.wallInputs) {
      showValue(field, input, room.walls[field.key]);
      input.disabled = field.key === 'height' && !room.walls.enabled;
    }
  }

  /** Поля плитки, клея и СВП выделенных комнат. */
  showTile(rooms, catalogs) {
    const tile = rooms[0].tile;
    const same = (key) => rooms.every((r) => r.tile[key] === tile[key]);
    const single = rooms.length === 1;
    this.enabled.checked = tile.enabled;
    this.enabled.indeterminate = !same('enabled');
    const samePattern = rooms.every((r) => patternOf(r.tile) === patternOf(tile));
    for (const { field, input, note } of this.inputs.values()) {
      const value = tile[field.key];
      const mixed = !same(field.key);
      input.disabled = (!single && (field.key === 'off_x' || field.key === 'off_y')) ||
        (field.when && samePattern && !field.when(tile)); // у рисунка это поле не действует
      if (note) {
        note.textContent = samePattern ? field.note(rooms[0]) : '';
        note.hidden = !note.textContent;
      }
      if (document.activeElement === input) continue; // не мешать вводу
      if (field.options) {
        input.selectedIndex = mixed ? field.options.length : nearestIndex(field.options, value);
      } else if (field.check) {
        input.checked = value;
        input.indeterminate = mixed;
      } else if (field.color) {
        input.value = value;
      } else {
        input.value = mixed ? '' : String(Math.round(value * 100) / 100);
        input.placeholder = mixed ? 'разные' : '';
      }
    }
    this.showZones(rooms[0], catalogs);
    const found = same('texture') && catalogs.find(tile.texture);
    this.colorOnly.hidden = rooms.every((r) => !r.tile.texture);
    this.textureImage.hidden = !found;
    if (found) this.textureImage.src = catalogs.imageUrl(found.catalog, found.tile);
    this.textureName.textContent = !same('texture')
      ? 'разные'
      : !tile.texture
        ? 'только цвет'
        : found
          ? found.tile.name
          : `нет в каталогах: ${tile.texture}`;
  }
}

/** Где кладётся полоса стен: spans — [низ, верх, низ, верх…], мм от пола. */
function spansText(spans) {
  if (!spans.length) return '';
  const parts = [];
  for (let i = 0; i < spans.length; i += 2) parts.push(`от ${Math.round(spans[i])} до ${Math.round(spans[i + 1])}`);
  return `Кладётся ${parts.join(' и ')} мм от пола`;
}

/** Список [подпись, номер в проекте]; выбранный остаётся, пока он есть. Пусто — недоступен. */
function fillChoice(select, items) {
  const previous = select.value;
  select.replaceChildren(...items.map(([title, index]) => new Option(title, String(index))));
  if (items.some(([, index]) => String(index) === previous)) select.value = previous;
  select.disabled = !items.length;
  if (!items.length) select.add(new Option('нет', ''));
}

/** Значение поля ввода: число, флажок, цвет или пункт списка. */
function showValue(field, input, value) {
  if (document.activeElement === input) return;
  if (field.options) input.selectedIndex = nearestIndex(field.options, value);
  else if (field.check) input.checked = Boolean(value);
  else if (field.color) input.value = value;
  else input.value = String(Math.round(value * 100) / 100);
}

function hint(text) {
  const p = document.createElement('p');
  p.className = 'hint';
  p.textContent = text;
  return p;
}

function nearestIndex(options, value) {
  if (typeof value === 'string') return Math.max(0, options.findIndex(([, option]) => option === value));
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
    swatch.style.background = type.color;
    const found = type.textured && catalogs.find(type.tile.texture);
    if (found) swatch.style.backgroundImage = `url("${catalogs.imageUrl(found.catalog, found.tile)}")`;
    const title = document.createElement('div');
    title.className = 'summary-title';
    title.textContent = found ? `${found.tile.name}, ${type.name}` : type.name;
    const buy = document.createElement('div');
    buy.className = 'summary-buy';
    buy.textContent = type.packs > 0
      ? `${integer.format(type.to_buy)} шт. → ${integer.format(type.packs)} уп. по ${type.tile.tile_pack}`
      : `${integer.format(type.to_buy)} шт. к покупке`;
    const details = [
      `нужно ${integer.format(type.count)} шт. (целых ${integer.format(type.whole)}) + запас ${type.tile.reserve}%`,
      `${decimal.format(type.area)} м² пола, к покупке ${decimal.format(type.buy_area)} м² · ${type.rooms.join(', ')}`,
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
  if (summary.grout.total_kg > 0) {
    root.append(paragraph(`Затирка: ${decimal.format(summary.grout.total_kg)} кг — ${packs(summary.grout.packs, 'кг', 'уп.')}`));
  }
  if (summary.primer.total_l > 0) {
    root.append(paragraph(`Грунт: ${decimal.format(summary.primer.total_l)} л — ${packs(summary.primer.packs, 'л', 'кан.')}`));
  }
  if (summary.crosses.total > 0) {
    root.append(paragraph(`Крестики: ${integer.format(summary.crosses.total)} шт. — ${packs(summary.crosses.packs, 'шт.', 'уп.')}`));
  }
  if (summary.waterproof.total_kg > 0) {
    root.append(paragraph(`Гидроизоляция: ${decimal.format(summary.waterproof.total_kg)} кг — ${packs(summary.waterproof.packs, 'кг', 'уп.')}; лента ${decimal.format(summary.waterproof.tape_m)} м`));
  }
  if (summary.profiles.thresholds_m > 0) {
    root.append(paragraph(`Пороги: ${decimal.format(summary.profiles.thresholds_m)} м — ${packs(summary.profiles.thresholds, 'мм', 'планк.')}`));
  }
  if (summary.profiles.corners_m > 0) {
    root.append(paragraph(`Уголки: ${decimal.format(summary.profiles.corners_m)} м — ${packs(summary.profiles.corners, 'мм', 'планк.')}`));
  }
  root.append(paragraph(`Подрезка: ${decimal.format(summary.cut_m)} пог. м резов`));
  showEstimate(root, summary.estimate);
  root.append(paragraph(`Площадь комнат: ${decimal.format(summary.room_area)} м², под плитку: ${decimal.format(summary.tiled_area)} м² (с полом под дверьми)` +
    (summary.wall_area > 0 ? `, стены: ${decimal.format(summary.wall_area)} м²` : '')));
}

const rubles = new Intl.NumberFormat('ru-RU', { style: 'currency', currency: 'RUB', maximumFractionDigits: 2 });

/** Смета: позиции с ценой и итог; без цен — подсказка, где их задать. */
function showEstimate(root, estimate) {
  const block = document.createElement('div');
  block.className = 'estimate';
  const title = document.createElement('h3');
  title.textContent = 'Смета';
  block.append(title);
  if (!estimate.lines.length) {
    block.append(paragraph('Задайте цены в разделе «Цены для сметы» у комнаты — здесь появится стоимость.'));
    root.append(block);
    return;
  }
  const table = document.createElement('table');
  for (const line of estimate.lines) {
    const row = table.insertRow();
    const name = row.insertCell();
    if (line.color) {
      const swatch = document.createElement('span');
      swatch.className = 'swatch small';
      swatch.style.background = line.color;
      name.append(swatch);
    }
    name.append(line.name);
    row.insertCell().textContent = `${decimal.format(line.quantity)} ${line.unit} × ${decimal.format(line.price)}`;
    row.insertCell().textContent = rubles.format(line.cost);
  }
  block.append(table);
  const totals = [`Материалы: ${rubles.format(estimate.materials)}`];
  if (estimate.work > 0) totals.push(`работа: ${rubles.format(estimate.work)}`);
  block.append(paragraph(totals.join('; ')));
  const total = paragraph(`Итого: ${rubles.format(estimate.total)}`);
  total.className = 'estimate-total';
  block.append(total);
  root.append(block);
}

function paragraph(text) {
  const p = document.createElement('p');
  p.textContent = text;
  return p;
}
