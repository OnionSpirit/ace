---
id: testing:CN6
source_id: CN6
kind: coverage
status: Открыто
source_status: ⬜
priority: Не указан
source_document: agents/TESTING.md
source_line: 876
rank: 72704
---

# CN6. `stdin_link`

```dispatch
label: Выполнить следующую фазу
intent: take-task
tool: codex
repo: ace
prompt: "{{vault}}/{{file}}"
```

## Архивная запись

- **Раздел:** 3.20 console.h.
- **Fixture:** `console_fixture`.
- **Пункт:** `CN6` — `stdin_link`.
- **Критерий:** stdin_link() возвращает валидный link.
- **Статус в архивном плане:** ⬜.
