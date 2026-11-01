---
id: "testing:IE6"
source_id: "IE6"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 622
---

# IE6. `entity_is_closed`

```dispatch
label: Выполнить следующую фазу
intent: take-task
tool: codex
repo: ace
prompt: "{{vault}}/{{file}}"
```

## Архивная запись

- **Раздел:** 3.10 io.h.
- **Fixture:** `io_entity_fixture`.
- **Пункт:** `IE6` — `entity_is_closed`.
- **Критерий:** is_closed() возвращает корректный статус.
- **Статус в архивном плане:** ✅.
