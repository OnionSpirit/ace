---
id: "testing:SP1"
source_id: "SP1"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 801
---

# SP1. `spawn_returns_handle`

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
- **Пункт:** `SP1` — `spawn_returns_handle`.
- **Критерий:** co_await spawn(task) → async_handle.
- **Статус в архивном плане:** ✅.
