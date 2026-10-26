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

    // ---- правка плана: предметы — {kind, index}; tolerance и grid — в мм
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
    object: (kind, index) => withString(kind, (k) => JSON.parse(text(module._tl_web_object(k, index)))),
    setPartitionThickness: (index, value) => module._tl_web_set_partition_thickness(index, value, now()) !== 0,
    setOpeningWidth: (index, value) => module._tl_web_set_opening_width(index, value, now()) !== 0,
    deleteObject: (kind, index) => withString(kind, (k) => module._tl_web_delete(k, index, now())) !== 0,

    summary: () => JSON.parse(text(module._tl_web_summary())),
    undo: () => module._tl_web_undo() !== 0,
    redo: () => module._tl_web_redo() !== 0,
    canUndo: () => module._tl_web_undo_count() > 0,
    canRedo: () => module._tl_web_redo_count() > 0,
  };
}
