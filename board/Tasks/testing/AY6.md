---
id: "testing:AY6"
source_id: "AY6"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 600
---

# AY6. `any_destructor`

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
- **Пункт:** `AY6` — `any_destructor`.
- **Критерий:** Деструктор с данными не падает.
- **Статус в архивном плане:** ✅.
