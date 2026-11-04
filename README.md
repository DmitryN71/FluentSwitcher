# Правила автопереключения

FluentSwitcher раз в день забирает отсюда списки, по которым решает, переключать ли слово: сокращения, сокращения с
точками, зоны сайтов и расширения файлов, слова перед числом. Так списки пополняются без новой версии программы. Сами
правила – как решается, переключать ли слово, – меняются только с программой (`src/AutoSwitch.h`).

- `rules.json` – для выпусков, `rules-beta.json` – для бет: новое правило сначала уходит в beta-файл, через несколько
  дней без жалоб – в основной.
- Программа берёт файлы из ветки `rules` (`https://raw.githubusercontent.com/DmitryN71/FluentSwitcher/rules/rules.json`).
  Туда их копирует workflow `Publish rules` (`.github/workflows/publish-rules.yml`), только вручную. Правка в `main` до
  публикации ни до кого не доходит.
- Файл `rules.json`, положенный рядом с `FluentSwitcher.exe`, главнее скачанного: так правила ставятся без сети.
- Списки «Переключать всегда» и «Не переключать» в настройках главнее файла.

## Формат

```json
{
  "schema": 1,
  "version": "2026.11.04.1",
  "min_app": "1.6.0-beta1",
  "lists": {
    "short_en": { "add": ["rdp"], "remove": [] }
  }
}
```

Каждый список – добавления (`add`) и удаления (`remove`) поверх встроенного в программу (`src/Rules.h`). Удаление
главнее добавления. Слова – строчными.

| Список | Что в нём | Пример |
|---|---|---|
| `short_en`, `short_ru` | Сокращения, которые словарь не решит: набранное не в той раскладке переключается | `usb` (ГЫИ), `инн` (BYY) |
| `keep_en`, `keep_ru` | Что набирают нарочно и не переводят | `ru` – не «кг» |
| `dotted_ru` | Русские сокращения с точками | `т.д.` (n/l/) |
| `number_en`, `number_ru` | Слово перед числом, до четырёх букв | `v` (v1.5), `рис` (рис.2) |
| `zones` | Зоны сайтов и расширения файлов в конце адреса | `ru`, `jpg` |
| `neutral_en` | Английские слова, которые пишут и среди русских | `ok`, `lol` |
| `not_word_ru` | Слова, которые знает словарь, но так не пишут | `еру` (the) |

`tools/check_rules.cpp` проверяет файлы в каждой сборке на GitHub: формат, буквы, повторы и совпадения с частыми
словами (сокращение, которое в другой раскладке – частое слово, переключало бы это слово).

---

# Auto switch rules

FluentSwitcher downloads these lists once a day: abbreviations, abbreviations with dots, site zones and file
extensions, words before a number. The lists grow without a new version of the program; the rules themselves
(`src/AutoSwitch.h`) change only with the program.

- `rules.json` is for releases, `rules-beta.json` for betas (a new rule goes there first).
- The program takes the files from the `rules` branch; the `Publish rules` workflow copies them there, by hand only.
- `rules.json` next to `FluentSwitcher.exe` wins over the downloaded one (offline use). The user's "Always switch" and
  "Never switch" lists win over the file.
- Each list is `add` and `remove` over the built-in one (`src/Rules.h`); `remove` wins; words in lower case.
  `tools/check_rules.cpp` checks the files in every GitHub build.
