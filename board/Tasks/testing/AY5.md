---
id: "testing:AY5"
source_id: "AY5"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 599
---

# AY5. `any_move`

```dispatch
label: Выполнить следующую фазу
intent: take-task
tool: codex
repo: ace
prompt: "{{vault}}/{{file}}"
```

## Архивная запись

- **Раздел:** 3.10 io.h.
- **Fixture:** `io_any_fixture`.
- **Пункт:** `AY5` — `any_move`.
- **Критерий:** Move-конструктор с exchange обнуляет источник.
- **Статус в архивном плане:** ✅.
