---
id: "testing:AH7"
source_id: "AH7"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 538
---

# AH7. `handle_done`

```dispatch
label: Выполнить следующую фазу
intent: take-task
tool: codex
repo: ace
prompt: "{{vault}}/{{file}}"
```

## Архивная запись

- **Раздел:** 3.9 core/async_handle.h.
- **Fixture:** `spawn_extra_fixture` (расширение spawn-тестов).
- **Пункт:** `AH7` — `handle_done`.
- **Критерий:** done() возвращает true после завершения.
- **Статус в архивном плане:** ✅.
