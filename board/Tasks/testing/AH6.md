---
id: "testing:AH6"
source_id: "AH6"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 537
---

# AH6. `join_after_cancel`

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
- **Пункт:** `AH6` — `join_after_cancel`.
- **Критерий:** join() на отменённой → false.
- **Статус в архивном плане:** ✅.
