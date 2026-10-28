// Инструменты рисования плана — как в настольной программе: прямоугольник и
// контур (на пустом месте — дом, внутри дома — комната из перегородок),
// перегородка двумя щелчками, проём щелчком по стене, линейка и деформационный
// шов двумя щелчками, труба (отверстие в плитке) — щелчком. Точки притягиваются ядром (tl_snap: углы, концы
// перегородок, грани стен, сетка, выравнивание; у линейки и шва —
// tl_measure_point: грани, оси стен, швы плитки).

const SNAP_PX = 10; // допуск притягивания, пиксели экрана
const CLOSE_PX = 9; // щелчок у первой точки контура замыкает его
const GREEN = '#0b8043';
const GUIDE = '#1a73e8';

export const TOOL_HINTS = {
  select:
    'Щёлкните комнату, перегородку, проём или стену дома и тяните. Выделенная комната тянет весь дом, с Ctrl — сетку плитки. Delete — удалить.',
  rect: 'Прямоугольник: щёлкните угол, затем противоположный угол. На пустом месте — дом, внутри дома — комната.',
  contour:
    'Контур: щёлкайте углы; замкнуть — щелчок по первой точке, двойной щелчок или Enter. Backspace — убрать точку, Esc — отмена.',
  partition: 'Перегородка: щёлкните начало и конец. Точки притягиваются к стенам и углам; Shift — без привязок.',
  opening: 'Проём: щёлкните по стене или перегородке.',
  ruler: 'Линейка: щёлкните начало и конец. Концы притягиваются к граням и осям стен и к швам плитки.',
  joint: 'Деформационный шов: щёлкните начало и конец. Шов режет плитку щелью заданной ширины.',
  divider:
    'Раздел: щёлкните начало и конец — линия делит пол комнаты на части. Концы притягиваются к стенам и другим линиям. Щёлкните часть, чтобы задать ей свою плитку или «без плитки».',
  pipe: 'Труба: щёлкните, где отверстие в плитке под трубу или слив. Плитка с отверстием — в чертежах подрезок.',
  drain: 'Трап: щёлкните, где он. Точечный режет плитку конвертом к углам, линейный встаёт к стене. Вид и уклон — на панели.',
  fixture: 'Предмет: щёлкните в комнате — у стены он встанет к ней спинкой (Shift — где щёлкнули). Плитку под ним и за ним — на панели.',
  underlay_calibrate:
    'Калибровка: введите настоящее расстояние, затем щёлкните его концы на подложке (например, концы стены с размером).',
  underlay_move: 'Сдвиг подложки: щёлкните точку на подложке, затем — куда её поставить (например, угол нарисованной стены).',
};

const PIPE_COLOR = '#3d5a80';

const MEASURE_TARGETS = { ruler: 'all', joint: 'walls', divider: 'walls' };

export class Tools {
  /**
   * env: {core, view, settings: {grid, houseThickness, partitionThickness, openingWidth, jointGap, pipeDiameter},
   *       edited(selection) — проект изменился, done() — рисование закончено (к «Выбору»), say(text)}
   */
  constructor(env) {
    this.env = env;
    this.tool = 'select';
    this.reset();
  }

  reset() {
    this.points = []; // поставленные точки (контур, начало перегородки, угол прямоугольника)
    this.cursor = null; // {point, guides, face}
    this.preview = null; // вырез проёма
  }

  set(tool) {
    this.tool = tool;
    this.reset();
    this.env.say(TOOL_HINTS[tool]);
  }

  get drawing() {
    return this.tool !== 'select';
  }

  tolerance() {
    return SNAP_PX / this.env.view.scale;
  }

  snap(world, free) {
    const { core, settings } = this.env;
    const start = this.points.length ? this.points[this.points.length - 1] : null;
    if (MEASURE_TARGETS[this.tool]) {
      const result = core.measurePoint(world[0], world[1], start, MEASURE_TARGETS[this.tool], this.tolerance(), free);
      return { point: result.point, guides: result.guides, face: null, snapped: result.snapped };
    }
    return core.snap(world[0], world[1], start, this.points.flat(), this.tolerance(), settings.grid, free);
  }

  /** Курсор над планом (мышь): предпросмотр. */
  hover(world, free) {
    if (!this.drawing) return;
    if (this.tool === 'opening') {
      const { core, settings } = this.env;
      this.preview = core.openingPreview(world[0], world[1], settings.openingWidth, this.tolerance(), settings.grid);
    } else {
      this.cursor = this.snap(world, free);
    }
  }

  /** Щелчок: следующая точка или готовый предмет. */
  click(world, free) {
    const { core, settings } = this.env;
    if (this.tool === 'opening') {
      const index = core.addOpening(world[0], world[1], settings.openingWidth, this.tolerance(), settings.grid);
      if (index < 0) {
        this.env.say('Щёлкните по стене или перегородке длиннее проёма.');
        return;
      }
      this.env.edited({ kind: 'opening', index });
      return;
    }
    const point = this.snap(world, free).point;
    if (this.tool === 'underlay_calibrate' || this.tool === 'underlay_move') {
      if (!this.points.length) {
        this.points = [point];
        return;
      }
      const start = this.points[0];
      const move = this.tool === 'underlay_move';
      this.points = [];
      const ok = move
        ? core.setUnderlay({ dx: point[0] - start[0], dy: point[1] - start[1] })
        : settings.calibration > 0 && core.calibrateUnderlay(start, point, settings.calibration);
      if (!ok) {
        this.env.say(move ? 'Не получилось сдвинуть подложку.'
          : settings.calibration > 0 ? 'Точки совпадают — щёлкните концы размера.' : 'Сначала введите настоящее расстояние.');
        return;
      }
      this.env.edited(null);
      this.env.done();
      this.env.say(move ? 'Подложка сдвинута.' : `Подложка откалибрована: ${settings.calibration} мм между точками.`);
      return;
    }
    if (this.tool === 'drain') {
      const index = core.addDrain(settings.drainKind, point, free);
      if (index >= 0) this.env.edited({ kind: 'drain', index });
      return;
    }
    if (this.tool === 'fixture') {
      const index = core.addFixture(settings.fixtureKind, world, free);
      if (index >= 0) this.env.edited({ kind: 'fixture', index });
      return;
    }
    if (this.tool === 'pipe') {
      const index = core.addPipe(point, settings.pipeDiameter);
      if (index >= 0) this.env.edited({ kind: 'pipe', index });
      return;
    }
    if (MEASURE_TARGETS[this.tool]) {
      if (!this.points.length) {
        this.points = [point];
        return;
      }
      const start = this.points[0];
      this.points = [];
      const index = this.tool === 'ruler' ? core.addRuler(start, point)
        : this.tool === 'divider' ? core.addDivider(start, point, settings.dividerGap)
          : core.addJoint(start, point, settings.jointGap);
      if (index < 0) this.env.say('Слишком коротко.');
      else this.env.edited({ kind: this.tool === 'ruler' ? 'ruler' : 'joint', index, part: 0, end: 0 });
      return;
    }
    if (this.tool === 'partition') {
      if (!this.points.length) {
        this.points = [point];
        return;
      }
      const index = core.addPartition(this.points[0], point, settings.partitionThickness);
      this.points = [];
      if (index < 0) this.env.say('Перегородка слишком короткая.');
      else this.env.edited({ kind: 'partition', index });
      return;
    }
    if (this.tool === 'rect') {
      if (!this.points.length) {
        this.points = [point];
        return;
      }
      const [a, b] = [this.points[0], point];
      this.points = [];
      this.finishContour([a, [b[0], a[1]], b, [a[0], b[1]]]);
      return;
    }
    // контур
    if (this.points.length >= 3 && this.nearFirst(point)) {
      this.finishContour(this.points);
      return;
    }
    const last = this.points[this.points.length - 1];
    if (!last || last[0] !== point[0] || last[1] !== point[1]) this.points.push(point);
  }

  nearFirst(point) {
    const first = this.points[0];
    return Math.hypot(point[0] - first[0], point[1] - first[1]) * this.env.view.scale < CLOSE_PX;
  }

  finishContour(points) {
    const { core, settings } = this.env;
    this.points = [];
    const result = core.addContour(points, settings.houseThickness, settings.partitionThickness);
    const messages = {
      invalid: 'Нужно хотя бы три угла и ненулевая площадь.',
      partitions: 'Перегородки добавлены, но замкнутой комнаты не получилось.',
    };
    if (messages[result.result]) this.env.say(messages[result.result]);
    if (result.result !== 'invalid') {
      this.env.edited(result.space ? { kind: 'space', space: result.space } : null);
      this.env.done();
      if (result.result === 'house') this.env.say('Дом добавлен. Внутри него рисуйте комнаты и перегородки.');
      if (result.result === 'room') this.env.say('Комната добавлена.');
    }
  }

  doubleClick() {
    if (this.tool === 'contour' && this.points.length >= 3) this.finishContour(this.points);
  }

  /** Клавиши инструмента; true — обработано. */
  key(event) {
    if (event.key === 'Escape' && this.points.length) {
      this.points = [];
      return true;
    }
    if (this.tool === 'contour' && event.key === 'Enter' && this.points.length >= 3) {
      this.finishContour(this.points);
      return true;
    }
    if (this.tool === 'contour' && event.key === 'Backspace' && this.points.length) {
      this.points.pop();
      return true;
    }
    return false;
  }

  // ---- рисование поверх плана (в пикселях экрана) ------------------------------

  draw(ctx) {
    if (!this.drawing) return;
    const view = this.env.view;
    const screen = (p) => view.toScreen(p[0], p[1]);
    ctx.save();
    if (this.tool === 'opening') {
      if (this.preview) {
        ctx.beginPath();
        const cut = this.preview.cut;
        for (let i = 0; i < cut.length; i += 2) ctx.lineTo(...screen([cut[i], cut[i + 1]]));
        ctx.closePath();
        ctx.fillStyle = 'rgba(11, 128, 67, 0.35)';
        ctx.fill();
        ctx.strokeStyle = GREEN;
        ctx.lineWidth = 1.5;
        ctx.stroke();
      }
      ctx.restore();
      return;
    }
    const cursor = this.cursor?.point;
    if (this.tool === 'pipe') {
      if (cursor) drawPipe(ctx, screen(cursor), this.env.settings.pipeDiameter * view.scale / 2, GREEN);
      ctx.restore();
      return;
    }
    if (this.cursor) {
      for (const [x, y, vertical] of this.cursor.guides) {
        const from = screen([x, y]);
        const to = screen(vertical ? [x, cursor[1]] : [cursor[0], y]);
        ctx.beginPath();
        ctx.moveTo(...from);
        ctx.lineTo(...to);
        ctx.setLineDash([4, 4]);
        ctx.strokeStyle = GUIDE;
        ctx.lineWidth = 1;
        ctx.stroke();
        ctx.setLineDash([]);
      }
      if (this.cursor.face) {
        const f = this.cursor.face;
        ctx.beginPath();
        ctx.moveTo(...screen([f[0], f[1]]));
        ctx.lineTo(...screen([f[2], f[3]]));
        ctx.strokeStyle = 'rgba(26, 115, 232, 0.6)';
        ctx.lineWidth = 3;
        ctx.stroke();
      }
    }
    const points = [...this.points];
    if (cursor && points.length) {
      if (this.tool === 'rect') {
        const a = points[0];
        points.push([cursor[0], a[1]], cursor, [a[0], cursor[1]], a);
      } else {
        points.push(cursor);
      }
    }
    if (points.length >= 2) {
      ctx.beginPath();
      points.forEach((p, i) => (i ? ctx.lineTo(...screen(p)) : ctx.moveTo(...screen(p))));
      if (this.tool === 'partition' || this.tool === 'joint' || this.tool === 'divider') {
        const width = this.tool === 'joint' ? this.env.settings.jointGap
          : this.tool === 'divider' ? this.env.settings.dividerGap : this.env.settings.partitionThickness;
        ctx.lineWidth = Math.max(2, width * view.scale);
        ctx.strokeStyle = 'rgba(11, 128, 67, 0.47)';
        ctx.lineCap = 'butt';
        ctx.stroke();
      }
      ctx.lineWidth = 1.5;
      ctx.strokeStyle = GREEN;
      ctx.stroke();
      const a = points[points.length - 2];
      const b = points[points.length - 1];
      if (this.tool !== 'rect') label(ctx, `${Math.round(Math.hypot(b[0] - a[0], b[1] - a[1]))}`, screen(a), screen(b));
      else {
        const corner = points[0];
        label(ctx, `${Math.round(Math.abs(cursor[0] - corner[0]))} × ${Math.round(Math.abs(cursor[1] - corner[1]))}`, screen(corner), screen(cursor));
      }
    }
    for (const p of this.points) dot(ctx, screen(p), GREEN, 4);
    if (cursor) cross(ctx, screen(cursor));
    if (cursor && this.cursor.snapped) dot(ctx, screen(cursor), '#c2410c', 4); // притянулось к стене или шву
    ctx.restore();
  }
}

/** Отверстие под трубу: круг коронки (не меньше 4 px) с крестом в центре. */
export function drawPipe(ctx, [x, y], radius, color = PIPE_COLOR) {
  const r = Math.max(4, radius);
  ctx.beginPath();
  ctx.arc(x, y, r, 0, 2 * Math.PI);
  ctx.fillStyle = '#e8eef6';
  ctx.fill();
  ctx.strokeStyle = color;
  ctx.lineWidth = 1.2;
  ctx.stroke();
  const c = r * 0.6;
  ctx.beginPath();
  ctx.moveTo(x - c, y);
  ctx.lineTo(x + c, y);
  ctx.moveTo(x, y - c);
  ctx.lineTo(x, y + c);
  ctx.lineWidth = 1;
  ctx.stroke();
}

function dot(ctx, [x, y], color, radius) {
  ctx.beginPath();
  ctx.arc(x, y, radius, 0, 2 * Math.PI);
  ctx.fillStyle = color;
  ctx.fill();
}

function cross(ctx, [x, y]) {
  ctx.beginPath();
  ctx.moveTo(x - 8, y);
  ctx.lineTo(x + 8, y);
  ctx.moveTo(x, y - 8);
  ctx.lineTo(x, y + 8);
  ctx.strokeStyle = GREEN;
  ctx.lineWidth = 1.5;
  ctx.stroke();
}

/** Подпись размера у середины отрезка. */
function label(ctx, text, a, b) {
  const x = (a[0] + b[0]) / 2;
  const y = (a[1] + b[1]) / 2 - 14;
  ctx.font = '600 12px "Segoe UI", Roboto, Arial, sans-serif';
  const width = ctx.measureText(text).width + 10;
  ctx.fillStyle = 'rgba(255, 255, 255, 0.92)';
  ctx.fillRect(x - width / 2, y - 9, width, 18);
  ctx.fillStyle = GREEN;
  ctx.textAlign = 'center';
  ctx.textBaseline = 'middle';
  ctx.fillText(text, x, y + 0.5);
}
