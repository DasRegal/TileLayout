// Файлы проекта: открыть с компьютера, сохранить на компьютер (тот же формат
// *.tilelayout.json, что у настольной программы) и черновик в браузере —
// последний проект этого пользователя. Сервера нет: у каждого — свои файлы.

const DRAFT_KEY = 'tilelayout.draft';
const NAME_KEY = 'tilelayout.draft-name';

export function readDraft() {
  try {
    const text = localStorage.getItem(DRAFT_KEY);
    return text ? { text, name: localStorage.getItem(NAME_KEY) || 'plan.tilelayout.json' } : null;
  } catch {
    return null; // закрытый режим или запрещено сайтом
  }
}

export function writeDraft(text, name) {
  try {
    localStorage.setItem(DRAFT_KEY, text);
    localStorage.setItem(NAME_KEY, name);
    return true;
  } catch {
    return false; // переполнено или запрещено — работаем без черновика
  }
}

/** Выбор файла с компьютера → {name, text} или null. */
export function pickFile() {
  return new Promise((resolve) => {
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = '.json,application/json';
    input.addEventListener('change', async () => {
      const file = input.files[0];
      resolve(file ? { name: file.name, text: await file.text() } : null);
    });
    input.addEventListener('cancel', () => resolve(null));
    input.click();
  });
}

/** Сохранить текст файлом name — загрузкой браузера. */
export function downloadFile(text, name) {
  const url = URL.createObjectURL(new Blob([text], { type: 'application/json' }));
  const link = document.createElement('a');
  link.href = url;
  link.download = name;
  document.body.append(link);
  link.click();
  link.remove();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

/** Файл, брошенный на element, → onFile({name, text}). */
export function acceptDroppedFiles(element, onFile) {
  element.addEventListener('dragover', (event) => {
    if ([...event.dataTransfer.types].includes('Files')) {
      event.preventDefault();
      element.classList.add('drop-target');
    }
  });
  element.addEventListener('dragleave', () => element.classList.remove('drop-target'));
  element.addEventListener('drop', async (event) => {
    event.preventDefault();
    element.classList.remove('drop-target');
    const file = event.dataTransfer.files[0];
    if (file) onFile({ name: file.name, text: await file.text() });
  });
}
