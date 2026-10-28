// Ядро TileLayout в WebAssembly (src/web/tl_web.h): тот же код, что у настольной
// программы. Обмен — строками JSON; строки передаются через память модуля
// (malloc), а не через стек: текст проекта бывает больше стека.

/* global createTileLayout */

export async function loadCore() {
  const module = await createTileLayout();

  function withString(text, call) {
    const size = module.lengthBytesUTF8(text) + 1;
    const pointer = module._malloc(size);
    module.stringToUTF8(text, pointer, size);
    try {
      return call(pointer);
    } finally {
      module._free(pointer);
    }
  }

  const text = (pointer) => module.UTF8ToString(pointer);
  /** Предмет плана полностью: у нарисованного только что нет part и end. */
  const item = ({ kind, index = 0, part = 0, end = 0 }) => ({ kind, index, part, end });
  const now = () => Date.now() / 1000;

  return {
    newDemo() {
      module._tl_web_new_demo();
    },
    /** Открыть проект из текста файла; ошибка — исключение с объяснением. */
    open(projectText) {
      if (!withString(projectText, (p) => module._tl_web_open(p))) {
        throw new Error(text(module._tl_web_error()));
      }
    },
    save: () => text(module._tl_web_save()),
    version: () => text(module._tl_web_version()),
    scene: () => JSON.parse(text(module._tl_web_scene())),
    spaceAt: (x, y) => module._tl_web_space_at(x, y),
    spaceTile: (id) => JSON.parse(text(module._tl_web_space_tile(id))),
    setTileField: (id, key, value) =>
      withString(key, (k) => withString(JSON.stringify(value), (v) => module._tl_web_set_tile_field(id, k, v, now()))) !== 0,
    setTileFields: (id, fields) =>
      withString(JSON.stringify(fields), (f) => module._tl_web_set_tile_fields(id, f, now())) !== 0,
    /** Поля плитки сразу нескольким комнатам (ids) — одним шагом отмены. */
    setRoomsTileFields: (ids, fields) =>
      withString(JSON.stringify(ids), (i) =>
        withString(JSON.stringify(fields), (f) => module._tl_web_set_rooms_tile_fields(i, f, now()))) !== 0,
    /** Зона ('border' | 'carpet' пола, 'wall_lower' | 'wall_frieze' | 'wall_main' — полосы стен) у комнат ids:
        {enabled, width, height, dx, dy, fit, поля плитки}. */
    setZoneFields: (ids, zone, fields) =>
      withString(JSON.stringify(ids), (i) =>
        withString(zone, (z) => withString(JSON.stringify(fields), (f) => module._tl_web_set_zone_fields(i, z, f, now())))) !== 0,
    /** Высота помещения и облицовка стен у комнат ids: {room_height, enabled, height}. */
    setWalls: (ids, fields) =>
      withString(JSON.stringify(ids), (i) =>
        withString(JSON.stringify(fields), (f) => module._tl_web_set_walls(i, f, now()))) !== 0,
    /** Развёртка стен комнаты (tl_web_walls.c) или null. */
    roomWalls: (id) => JSON.parse(text(module._tl_web_room_walls(id))),
    // ---- на развёртке комнаты id: wall — номер стены, точка на стене — [x от левого угла, h от пола]
    setWallOffset: (id, wall, x, y) => module._tl_web_set_wall_offset(id, wall, x, y, now()) !== 0,
    /** «Швы как у пола» у стены wall или у всех (−1) → у скольких совмещено. */
    alignWallsToFloor: (id, wall) => module._tl_web_wall_align_floor(id, wall, now()),
    addWallPipe: (id, wall, [x, h], diameter) => module._tl_web_add_wall_pipe(id, wall, x, h, diameter, now()),
    setWallPipe: (id, index, wall, [x, h], diameter) => module._tl_web_set_wall_pipe(id, index, wall, x, h, diameter, now()) !== 0,
    addWallRuler: (id, wall, [x0, h0], [x1, h1]) => module._tl_web_add_wall_ruler(id, wall, x0, h0, x1, h1, now()),
    setWallRuler: (id, index, [x0, h0], [x1, h1]) => module._tl_web_set_wall_ruler(id, index, x0, h0, x1, h1, now()) !== 0,
    /** Панно на стене wall развёртки или на полу (−1) → номер или −1; правка — поля {columns, rows, w, h, gap, x, y,
        price, color, texture, wall}. */
    addPanno: (id, wall) => module._tl_web_add_panno(id, wall, now()),
    setPanno: (id, index, fields) => withString(JSON.stringify(fields), (f) => module._tl_web_set_panno(id, index, f, now())) !== 0,
    /** kind — 'wall_pipe' | 'wall_ruler' | 'panno'. */
    deleteWallItem: (id, kind, index) => withString(kind, (k) => module._tl_web_delete_wall_item(id, k, index, now())) !== 0,
    /** Точка развёртки, притянутая к углам, полу, проёмам и швам → [x, y]. */
    wallsSnap: (id, x, y, tolerance) => JSON.parse(text(module._tl_web_walls_snap(id, x, y, tolerance))),
    swapTiles: (ids) => withString(JSON.stringify(ids), (i) => module._tl_web_swap_tiles(i, now())) !== 0,
    /** Раскладка комнаты по правилу (от угла, по центру, минимум плиток) → {ok, message}. */
    alignTiles: (id, mode) => withString(mode, (m) => JSON.parse(text(module._tl_web_align_tiles(id, m, now())))),
    setSpaceName: (id, name) => withString(name, (n) => module._tl_web_set_space_name(id, n, now())) !== 0,
    setSpaceSource: (id, source) => module._tl_web_set_space_source(id, source, now()) !== 0,
    /** Точка угла дома, узла или примыкания → предмет после правки или null. */
    setPoint(target, x, y) {
      const { kind, index, part, end } = item(target);
      return withString(kind, (k) => JSON.parse(text(module._tl_web_set_point(k, index, part, end, x, y, now()))));
    },
    setWallLength: (house, wall, length, outer, keepAngles) =>
      module._tl_web_set_wall_length(house, wall, length, outer ? 1 : 0, keepAngles ? 1 : 0, now()) !== 0,
    placeOpening: (index, distance, fromEnd) => module._tl_web_place_opening(index, distance, fromEnd ? 1 : 0, now()) !== 0,
    setThreshold: (index, right, skew) => module._tl_web_set_threshold(index, right, skew, now()) !== 0,
    setLocked(target, locked) {
      const { kind, index, part } = item(target);
      return withString(kind, (k) => module._tl_web_set_locked(k, index, part, locked ? 1 : 0, now())) !== 0;
    },
    insertVertex: (house, wall, x, y, grid) => module._tl_web_insert_vertex(house, wall, x, y, grid, now()) !== 0,
    addHouse: (width, height, thickness) => module._tl_web_add_house(width, height, thickness, now()),

    // ---- правка плана: предметы — {kind, index, part, end} (tl_web_pick); tolerance и grid — в мм
    snap: (x, y, start, refs, tolerance, grid, free) =>
      withString(JSON.stringify(refs), (r) =>
        JSON.parse(text(module._tl_web_snap(x, y, start ? 1 : 0, start?.[0] ?? 0, start?.[1] ?? 0, r, tolerance, grid, free ? 1 : 0))),
      ),
    addContour: (points, houseThickness, partitionThickness) =>
      withString(JSON.stringify(points.flat()), (p) =>
        JSON.parse(text(module._tl_web_add_contour(p, houseThickness, partitionThickness, now()))),
      ),
    addPartition: (a, b, thickness) => module._tl_web_add_partition(a[0], a[1], b[0], b[1], thickness, now()),
    openingPreview: (x, y, width, tolerance, grid) =>
      JSON.parse(text(module._tl_web_opening_preview(x, y, width, tolerance, grid))),
    addOpening: (x, y, width, tolerance, grid) => module._tl_web_add_opening(x, y, width, tolerance, grid, now()),
    pick: (x, y, tolerance) => JSON.parse(text(module._tl_web_pick(x, y, tolerance))),
    object(target) {
      const { kind, index, part, end } = item(target);
      return withString(kind, (k) => JSON.parse(text(module._tl_web_object(k, index, part, end))));
    },
    setPartitionThickness: (index, value) => module._tl_web_set_partition_thickness(index, value, now()) !== 0,
    setPartitionLength: (index, value) => module._tl_web_set_partition_length(index, value, now()) !== 0,
    setPartitionAngle: (index, value) => module._tl_web_set_partition_angle(index, value, now()) !== 0,
    setOpeningWidth: (index, value) => module._tl_web_set_opening_width(index, value, now()) !== 0,
    /** Вид проёма ('door' | 'window' | 'arch'; '' — прежний), высота и низ от пола, мм. */
    setOpeningReveal: (index, depth) => module._tl_web_set_opening_reveal(index, depth, now()) !== 0,
    setOpening: (index, kind, height, sill) =>
      withString(kind, (k) => module._tl_web_set_opening(index, k, height, sill, now())) !== 0,
    addRuler: (a, b) => module._tl_web_add_ruler(a[0], a[1], b[0], b[1], now()),
    addJoint: (a, b, gap) => module._tl_web_add_joint(a[0], a[1], b[0], b[1], gap, now()),
    /** Предмет ('bath' | 'tray' | 'installation' | 'cabinet' | 'kitchen' | 'stairs'); у стены — спинкой к ней (free — нет). */
    addFixture: (kind, point, free) => withString(kind, (k) => module._tl_web_add_fixture(k, point[0], point[1], free ? 1 : 0, now())),
    /** Поля предмета: {kind, w, d, h, angle, x, y, under, behind}. */
    setFixture: (index, fields) => withString(JSON.stringify(fields), (f) => module._tl_web_set_fixture(index, f, now())) !== 0,
    /** Трап ('point' | 'linear'); линейный у стены — вдоль неё (free — нет). */
    addDrain: (kind, point, free) => withString(kind, (k) => module._tl_web_add_drain(k, point[0], point[1], free ? 1 : 0, now())),
    /** Поля трапа: {kind, size, length, angle, slope, x, y, envelope}. */
    setDrain: (index, fields) => withString(JSON.stringify(fields), (f) => module._tl_web_set_drain(index, f, now())) !== 0,
    /** Линия раздела покрытия (шов gap мм) → номер среди швов или −1. */
    addDivider: (a, b, gap) => module._tl_web_add_divider(a[0], a[1], b[0], b[1], gap, now()),
    /** Часть пола region: covering 'room' | 'own' | 'none' | '' (не менять), name — название участка ('' — не менять). */
    setArea: (id, region, covering, name = '') =>
      withString(covering, (c) => withString(name, (n) => module._tl_web_set_area(id, region, c, n, now()))) !== 0,
    setJointGap: (index, gap) => module._tl_web_set_joint_gap(index, gap, now()) !== 0,
    /** Отверстие под трубу (диаметр коронки, мм) → номер или −1. */
    addPipe: (point, diameter) => module._tl_web_add_pipe(point[0], point[1], diameter, now()),
    setPipe: (index, x, y, diameter) => module._tl_web_set_pipe(index, x, y, diameter, now()) !== 0,
    /** Имя линейки или проёма ({kind: 'ruler' | 'opening', index}). */
    setName: ({ kind, index }, name) =>
      withString(kind, (k) => withString(name, (n) => module._tl_web_set_name(k, index, n, now()))) !== 0,
    /** Конец линейки (targets 'all') или шва ('walls'): притягивание и выравнивание от anchor. */
    measurePoint: (x, y, anchor, targets, tolerance, free) =>
      withString(targets, (t) =>
        JSON.parse(text(module._tl_web_measure_point(x, y, anchor ? 1 : 0, anchor?.[0] ?? 0, anchor?.[1] ?? 0, t, tolerance, free ? 1 : 0))),
      ),
    setWallThickness: (house, wall, value) => module._tl_web_set_wall_thickness(house, wall, value, now()) !== 0,
    deleteObject(target) {
      const { kind, index, part, end } = item(target);
      return withString(kind, (k) => module._tl_web_delete(k, index, part, end, now())) !== 0;
    },
    /** Перетаскивание: begin → move… → end; ответ end — предмет после правки. */
    dragBegin(target, x, y) {
      const { kind, index, part, end } = item(target);
      return withString(kind, (k) => module._tl_web_drag_begin(k, index, part, end, x, y)) !== 0;
    },
    dragMove: (x, y, tolerance, grid, free) =>
      JSON.parse(text(module._tl_web_drag_move(x, y, tolerance, grid, free ? 1 : 0, now()))),
    dragEnd: () => JSON.parse(text(module._tl_web_drag_end())),

    summary: () => JSON.parse(text(module._tl_web_summary())),
    tileNumbers: () => JSON.parse(text(module._tl_web_tile_numbers())),
    cutCards: () => JSON.parse(text(module._tl_web_cut_cards())),
    cutMap: () => JSON.parse(text(module._tl_web_cut_map())),
    /** Подложка с содержимым: {present, revision, origin, scale, opacity, visible, image, image_w, image_h, lines}. */
    underlay: () => JSON.parse(text(module._tl_web_underlay())),
    setUnderlayImage: (url, width, height) =>
      withString(url, (u) => module._tl_web_set_underlay_image(u, width, height, now())) !== 0,
    /** DXF-план текстом; ошибка — исключение с объяснением. */
    loadUnderlayDxf(dxf) {
      if (!withString(dxf, (d) => module._tl_web_load_underlay_dxf(d, now()))) throw new Error(text(module._tl_web_error()));
    },
    setUnderlay: (fields) => withString(JSON.stringify(fields), (f) => module._tl_web_set_underlay(f, now())) !== 0,
    calibrateUnderlay: ([ax, ay], [bx, by], distance) =>
      module._tl_web_calibrate_underlay(ax, ay, bx, by, distance, now()) !== 0,
    clearUnderlay: () => module._tl_web_clear_underlay(now()),
    /** Чертёж DXF: cuts — подрезки для станка, иначе план. */
    dxf: (cuts) => text(module._tl_web_dxf(cuts ? 1 : 0)),
    undo: () => module._tl_web_undo() !== 0,
    redo: () => module._tl_web_redo() !== 0,
    canUndo: () => module._tl_web_undo_count() > 0,
    canRedo: () => module._tl_web_redo_count() > 0,
  };
}
