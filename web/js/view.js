// Вид плана: масштаб (пикселей на мм) и сдвиг; колесо — масштаб у курсора,
// перетаскивание — сдвиг, два пальца — масштаб и сдвиг; щелчок без
// перетаскивания — onClick(точка в мм, событие); движение мыши без кнопок —
// onHover(точка в мм, событие).

const MIN_SCALE = 0.005; // пикселей на мм: дом 100 м — 500 px
const MAX_SCALE = 5;
const CLICK_SLOP = 5; // px: дальше — уже перетаскивание

export class View {
  constructor() {
    this.scale = 0.1;
    this.panX = 0;
    this.panY = 0;
  }

  toScreen(x, y) {
    return [x * this.scale + this.panX, y * this.scale + this.panY];
  }

  toWorld(sx, sy) {
    return [(sx - this.panX) / this.scale, (sy - this.panY) / this.scale];
  }

  /** Вписать прямоугольник [left, top, right, bottom] (мм) в width×height px с полями. */
  fit(bounds, width, height, margin = 56) {
    const [left, top, right, bottom] = bounds;
    const w = Math.max(right - left, 1);
    const h = Math.max(bottom - top, 1);
    this.scale = clamp(Math.min((width - 2 * margin) / w, (height - 2 * margin) / h), MIN_SCALE, MAX_SCALE);
    this.panX = (width - w * this.scale) / 2 - left * this.scale;
    this.panY = (height - h * this.scale) / 2 - top * this.scale;
  }

  /** Масштаб в factor раз, точка экрана (sx, sy) остаётся на месте. */
  zoomAt(sx, sy, factor) {
    const scale = clamp(this.scale * factor, MIN_SCALE, MAX_SCALE);
    const [x, y] = this.toWorld(sx, sy);
    this.scale = scale;
    this.panX = sx - x * scale;
    this.panY = sy - y * scale;
  }
}

function clamp(value, min, max) {
  return Math.min(max, Math.max(min, value));
}

/** Управление видом мышью и пальцами на элементе element. */
export function attachViewControls(element, view, { onChange, onClick, onHover }) {
  const pointers = new Map(); // id → [x, y]
  let start = null; // где нажали
  let dragged = false;
  let pinch = null; // {distance, middle}

  const local = (event) => {
    const box = element.getBoundingClientRect();
    return [event.clientX - box.left, event.clientY - box.top];
  };

  element.addEventListener(
    'wheel',
    (event) => {
      event.preventDefault();
      const [sx, sy] = local(event);
      const lines = event.deltaMode === 1 ? 16 : 1; // прокрутка строками (Firefox)
      view.zoomAt(sx, sy, Math.exp(-event.deltaY * lines * 0.0015));
      onChange();
    },
    { passive: false },
  );

  element.addEventListener('pointerdown', (event) => {
    element.setPointerCapture(event.pointerId);
    pointers.set(event.pointerId, local(event));
    if (pointers.size === 1) {
      start = local(event);
      dragged = false;
    }
    pinch = pointers.size === 2 ? pinchState() : null;
  });

  element.addEventListener('pointermove', (event) => {
    const previous = pointers.get(event.pointerId);
    if (!previous) {
      if (event.pointerType === 'mouse' && onHover) onHover(view.toWorld(...local(event)), event);
      return;
    }
    const current = local(event);
    pointers.set(event.pointerId, current);
    if (pointers.size === 2 && pinch) {
      const next = pinchState();
      view.zoomAt(pinch.middle[0], pinch.middle[1], next.distance / pinch.distance);
      view.panX += next.middle[0] - pinch.middle[0];
      view.panY += next.middle[1] - pinch.middle[1];
      pinch = next;
      dragged = true;
      onChange();
      return;
    }
    if (!event.buttons) return;
    view.panX += current[0] - previous[0];
    view.panY += current[1] - previous[1];
    dragged = dragged || Math.hypot(current[0] - start[0], current[1] - start[1]) > CLICK_SLOP;
    onChange();
  });

  const release = (event) => {
    const at = pointers.get(event.pointerId);
    pointers.delete(event.pointerId);
    if (pointers.size < 2) pinch = null;
    if (event.type === 'pointerup' && at && !dragged && pointers.size === 0) {
      onClick(view.toWorld(at[0], at[1]), event);
    }
  };
  element.addEventListener('pointerup', release);
  element.addEventListener('pointercancel', release);

  function pinchState() {
    const [a, b] = [...pointers.values()];
    return { distance: Math.max(Math.hypot(a[0] - b[0], a[1] - b[1]), 1), middle: [(a[0] + b[0]) / 2, (a[1] + b[1]) / 2] };
  }
}
