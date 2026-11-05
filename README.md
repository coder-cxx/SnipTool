# SnipTool

Простая утилита «Ножницы + рисовалка» на чистом WinAPI + GDI + stb_image_write.

## Возможности

- Глобальная горячая клавиша **Ctrl + Shift + S**
- Затемнение экрана + выбор области мышью (крестик, рамка, размер)
- Редактор: свободное рисование, линия, прямоугольник
- Выбор цвета
- Undo (кнопка или Ctrl+Z)
- Копирование в буфер обмена (стандартный CF_BITMAP)
- Сохранение в PNG или BMP
- Иконка в трее (клик → выход)
- Ничего никуда не отправляет, не прячется, не инжектится

## Сборка

### Локально (Visual Studio / Build Tools)

```bat
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Или через Developer Command Prompt:

```bat
cmake -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Готовый `SnipTool.exe` будет в папке `build`.

### GitHub Actions

Просто запушьте репозиторий на GitHub.  
Workflow `.github/workflows/build.yml` автоматически соберёт `SnipTool.exe` и выложит его как artifact.

Можно скачать из вкладки **Actions** → последний run → Artifacts → `SnipTool-win64`.

## Использование

1. Запустите `SnipTool.exe`
2. Нажмите **Ctrl + Shift + S**
3. Выделите область мышью (Esc — отмена)
4. В редакторе:
   - Pencil / Line / Rect — инструменты
   - Color — выбор цвета
   - Undo — отменить последнее
   - Copy — в буфер обмена
   - Save PNG — сохранить файл
5. Закрыть редактор — крестик или Esc
6. Выйти из программы — клик по иконке в трее

## Зависимости

- Только Windows API (user32, gdi32, gdiplus, comdlg32, shell32)
- `stb_image_write.h` (уже лежит в `include/`, single-header)

Никаких внешних DLL не требуется (статическая линковка CRT при сборке MinGW, при MSVC — обычный динамический CRT).

## Примечание

Если на системе стоит ПО, которое специально блокирует захват экрана (хуки GDI / Desktop Duplication / анти-скриншот), обычный `BitBlt` тоже может не сработать. В этом случае проблему нужно решать на уровне политики безопасности / админа. Эта утилита не содержит техник обхода защитных механизмов.