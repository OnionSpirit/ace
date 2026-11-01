---
id: "testing:AR1"
source_id: "AR1"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 945
---

# AR1. `small_alloc_served_from_pool`

```dispatch
label: Выполнить следующую фазу
intent: take-task
tool: codex
repo: ace
prompt: "{{vault}}/{{file}}"
```

## Архивная запись

- **Раздел:** 3.22 core/arena.h — arena_fixture.
- **Fixture:** `backup_fixture` (добавлен).
- **Пункт:** `AR1` — `small_alloc_served_from_pool`.
- **Критерий:** малая аллокация, выравнивание, accounting и retention пула.
- **Статус в архивном плане:** ✅.
