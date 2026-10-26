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
    scene: () => JSON.parse(text(module._tl_web_scene())),
    spaceAt: (x, y) => module._tl_web_space_at(x, y),
    spaceTile: (id) => JSON.parse(text(module._tl_web_space_tile(id))),
    setTileField: (id, key, value) =>
      withString(key, (k) => withString(JSON.stringify(value), (v) => module._tl_web_set_tile_field(id, k, v, now()))) !== 0,
    setTileFields: (id, fields) =>
      withString(JSON.stringify(fields), (f) => module._tl_web_set_tile_fields(id, f, now())) !== 0,
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
    addRuler: (a, b) => module._tl_web_add_ruler(a[0], a[1], b[0], b[1], now()),
    addJoint: (a, b, gap) => module._tl_web_add_joint(a[0], a[1], b[0], b[1], gap, now()),
    setJointGap: (index, gap) => module._tl_web_set_joint_gap(index, gap, now()) !== 0,
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
    undo: () => module._tl_web_undo() !== 0,
    redo: () => module._tl_web_redo() !== 0,
    canUndo: () => module._tl_web_undo_count() > 0,
    canRedo: () => module._tl_web_redo_count() > 0,
  };
}
