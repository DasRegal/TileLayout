// Каталоги плитки на сайте: список — catalogs.json (составляется при сборке
// сайта программой tl_catalog_index тем же загрузчиком, что у настольной
// версии), картинки — catalogs/<каталог>/<путь>. Ссылка на плитку в проекте —
// «каталог/плитка», как в настольной программе.

export class Catalogs {
  constructor(onImageLoaded) {
    this.list = [];
    this.images = new Map(); // ссылка → HTMLImageElement (или null — не загрузилась)
    this.onImageLoaded = onImageLoaded;
  }

  async load(url = 'catalogs.json') {
    try {
      const response = await fetch(url);
      this.list = response.ok ? await response.json() : [];
    } catch {
      this.list = [];
    }
  }

  find(reference) {
    const slash = reference ? reference.indexOf('/') : -1;
    if (slash < 0) return null;
    const catalog = this.list.find((c) => c.id === reference.slice(0, slash));
    const tile = catalog?.tiles.find((t) => t.id === reference.slice(slash + 1));
    return tile ? { catalog, tile } : null;
  }

  imageUrl(catalog, tile) {
    return ['catalogs', catalog.id, ...tile.image.split('/')].map(encodeURIComponent).join('/');
  }

  /** Картинка плитки, если уже загружена; иначе начинает загрузку и даёт null. */
  image(reference) {
    if (!reference) return null;
    if (this.images.has(reference)) {
      const image = this.images.get(reference);
      return image && image.complete && image.naturalWidth ? image : null;
    }
    const found = this.find(reference);
    if (!found) {
      this.images.set(reference, null);
      return null;
    }
    const image = new Image();
    image.onload = () => this.onImageLoaded();
    image.onerror = () => this.images.set(reference, null);
    image.src = this.imageUrl(found.catalog, found.tile);
    this.images.set(reference, image);
    return null;
  }

  /** Настройки комнаты от плитки: размер, рисунок, цвет (нет в описании — средний по рисунку). */
  async fieldsFor(catalog, tile) {
    let color = tile.color;
    if (!color) color = await averageColor(this.imageUrl(catalog, tile));
    const fields = { w: tile.width, h: tile.height, texture: `${catalog.id}/${tile.id}` };
    if (color) fields.color = color;
    return fields;
  }
}

function averageColor(url) {
  return new Promise((resolve) => {
    const image = new Image();
    image.onload = () => {
      const canvas = document.createElement('canvas');
      canvas.width = canvas.height = 16;
      const ctx = canvas.getContext('2d');
      ctx.drawImage(image, 0, 0, 16, 16);
      const pixels = ctx.getImageData(0, 0, 16, 16).data;
      const sum = [0, 0, 0];
      for (let i = 0; i < pixels.length; i += 4) for (let c = 0; c < 3; c++) sum[c] += pixels[i + c];
      const hex = sum.map((v) => Math.round(v / 256).toString(16).padStart(2, '0')).join('');
      resolve(`#${hex}`);
    };
    image.onerror = () => resolve('');
    image.src = url;
  });
}

// ---- окно выбора ------------------------------------------------------------------

/**
 * Окно выбора плитки: слева — каталоги и разделы (вложенные папки), справа —
 * плитки выбранного раздела с поиском. Возвращает {catalog, tile},
 * 'color-only' («Только цвет») или null (отмена).
 */
export function chooseTile(catalogs, currentReference) {
  const dialog = document.getElementById('catalog-dialog');
  const tree = dialog.querySelector('.catalog-tree');
  const grid = dialog.querySelector('.catalog-grid');
  const search = dialog.querySelector('#catalog-search');
  const choose = dialog.querySelector('[data-action="choose"]');
  let filter = { catalog: null, group: '' };
  let selected = catalogs.find(currentReference);

  const inGroup = (tile, group) => !group || tile.group === group || tile.group.startsWith(`${group}/`);

  function renderTree() {
    tree.replaceChildren();
    const total = catalogs.list.reduce((sum, c) => sum + c.tiles.length, 0);
    const root = treeItem('Все каталоги', total, null, '');
    tree.append(root.item);
    const rootList = document.createElement('ul');
    root.item.append(rootList);
    for (const catalog of catalogs.list) {
      const node = treeItem(catalog.name, catalog.tiles.length, catalog, '');
      rootList.append(node.item);
      const groups = new Map([['', node.item]]); // группа → <li>
      const groupsSorted = [...new Set(catalog.tiles.map((t) => t.group).filter(Boolean))].sort();
      for (const group of groupsSorted) ensureGroup(catalog, group, groups);
    }
    markActive();
  }

  function ensureGroup(catalog, group, groups) {
    if (groups.has(group)) return groups.get(group);
    const slash = group.lastIndexOf('/');
    const parent = ensureGroup(catalog, slash < 0 ? '' : group.slice(0, slash), groups);
    let list = parent.querySelector(':scope > ul');
    if (!list) {
      list = document.createElement('ul');
      parent.append(list);
    }
    const count = catalog.tiles.filter((t) => inGroup(t, group)).length;
    const node = treeItem(group.slice(slash + 1), count, catalog, group);
    list.append(node.item);
    groups.set(group, node.item);
    return node.item;
  }

  function treeItem(name, count, catalog, group) {
    const item = document.createElement('li');
    const button = document.createElement('button');
    button.type = 'button';
    button.className = 'tree-row';
    button.innerHTML = `<span></span> <span class="count"></span>`;
    button.children[0].textContent = name;
    button.children[1].textContent = count;
    button.addEventListener('click', () => {
      filter = { catalog, group };
      markActive();
      renderGrid();
    });
    button.dataset.catalog = catalog ? catalog.id : '';
    button.dataset.group = group;
    item.append(button);
    return { item, button };
  }

  function markActive() {
    for (const button of tree.querySelectorAll('.tree-row')) {
      const active = button.dataset.catalog === (filter.catalog?.id ?? '') && button.dataset.group === filter.group;
      button.classList.toggle('active', active);
    }
  }

  function matches(tile) {
    const query = search.value.trim().toLowerCase();
    if (!query) return true;
    const sizes = `${tile.width}×${tile.height} ${tile.width}x${tile.height} ${tile.width}х${tile.height}`;
    return tile.name.toLowerCase().includes(query) || sizes.includes(query);
  }

  function renderGrid() {
    grid.replaceChildren();
    for (const catalog of catalogs.list) {
      if (filter.catalog && filter.catalog !== catalog) continue;
      for (const tile of catalog.tiles) {
        if (!inGroup(tile, filter.group) || !matches(tile)) continue;
        grid.append(card(catalog, tile));
      }
    }
    if (!grid.children.length) {
      const empty = document.createElement('p');
      empty.className = 'empty';
      empty.textContent = 'Плиток не найдено. Уберите часть запроса или выберите другой раздел.';
      grid.append(empty);
    }
    choose.disabled = !selected;
  }

  function card(catalog, tile) {
    const button = document.createElement('button');
    button.type = 'button';
    button.className = 'tile-card';
    button.title = tile.group ? `${catalog.name} / ${tile.group}` : catalog.name;
    const picture = document.createElement('img');
    picture.loading = 'lazy';
    picture.alt = '';
    picture.src = catalogs.imageUrl(catalog, tile);
    const name = document.createElement('span');
    name.className = 'name';
    name.textContent = tile.name;
    const size = document.createElement('span');
    size.className = 'size';
    size.textContent = `${tile.width} × ${tile.height} мм`;
    button.append(picture, name, size);
    if (selected && selected.tile === tile) button.classList.add('selected');
    button.addEventListener('click', () => {
      selected = { catalog, tile };
      for (const other of grid.querySelectorAll('.selected')) other.classList.remove('selected');
      button.classList.add('selected');
      choose.disabled = false;
    });
    button.addEventListener('dblclick', () => dialog.close('choose'));
    return button;
  }

  return new Promise((resolve) => {
    search.value = '';
    search.oninput = renderGrid;
    renderTree();
    renderGrid();
    dialog.onclose = () => {
      const action = dialog.returnValue;
      if (action === 'choose' && selected) resolve(selected);
      else resolve(action === 'color-only' ? 'color-only' : null);
    };
    dialog.returnValue = '';
    dialog.showModal();
    search.focus();
  });
}
