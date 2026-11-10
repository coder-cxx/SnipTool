# SnipTool

Простая утилита «Ножницы + рисовалка» на чистом WinAPI + GDI + stb_image_write.

## Возможности

- Глобальная горячая клавиша **Ctrl + Shift + S**
- Затемнение экрана (агрессивное через UpdateLayeredWindow) + выбор области мышью
- Редактор сразу после выделения:
  - Pencil / Line / Rect / **Arrow**
  - Палитра из 8 цветов
  - Undo (кнопка + Ctrl+Z)
  - **Автоматическое копирование в буфер обмена** при закрытии редактора (как в Ножницах Microsoft) — рисунки тоже попадают в буфер
  - Save PNG / BMP
- Контекстное меню в трее (правый клик) → Capture / Exit
- Двойной клик по иконке в трее = захват
- Иконка **вшита в ресурсы** exe (из `resources/scissor.ico`)

## Иконка

Положи свою мульти-размерную иконку сюда:

```
resources/scissor.ico
```

Она автоматически вшивается в exe при сборке через `.rc` файл.

## Сборка

### Локально (Visual Studio / Build Tools)

```bat
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Готовый `SnipTool.exe` будет в папке `build`.

### GitHub Actions

Просто запушьте репозиторий на GitHub (вместе с `resources/scissor.ico`).  
Workflow автоматически соберёт `SnipTool.exe` и выложит его как artifact.

### Релизы

```bash
git tag v1.0.0
git push origin v1.0.0
```

Workflow создаст GitHub Release с `SnipTool.exe`.

## Использование

1. Запустите `SnipTool.exe`
2. Нажмите **Ctrl + Shift + S**
3. Выделите область мышью (Esc — отмена)
4. В редакторе рисуйте (Pencil / Line / Rect / Arrow), выбирайте цвет из палитры
5. Закройте окно редактора (крестик или Esc) — **готовое изображение с рисунками автоматически попадает в буфер обмена**
6. Выйти из программы — правый клик по иконке в трее → **Exit**

## Зависимости

- Только Windows API (user32, gdi32, msimg32, comdlg32, shell32)
- `stb_image_write.h` (уже лежит в `include/`)

Никаких внешних DLL и внешних `.ico` рядом с exe не требуется — всё внутри.
