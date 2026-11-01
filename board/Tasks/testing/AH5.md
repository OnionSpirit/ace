---
id: "testing:AH5"
source_id: "AH5"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 536
---

# AH5. `spawn_and_join`

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
- **Пункт:** `AH5` — `spawn_and_join`.
- **Критерий:** spawn → done-опрос → задача завершена.
- **Статус в архивном плане:** ✅.
