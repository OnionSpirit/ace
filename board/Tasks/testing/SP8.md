---
id: "testing:SP8"
source_id: "SP8"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 808
---

# SP8. `polling_true`

```dispatch
label: Выполнить следующую фазу
intent: take-task
tool: codex
repo: ace
prompt: "{{vault}}/{{file}}"
```

## Архивная запись

- **Раздел:** 3.17 Управление задачами.
- **Fixture:** `spawn_extra_fixture` (расширение).
- **Пункт:** `SP8` — `polling_true`.
- **Критерий:** polling(true) → задача идёт в _service_pool.
- **Статус в архивном плане:** ✅.
