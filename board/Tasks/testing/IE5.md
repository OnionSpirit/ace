---
id: "testing:IE5"
source_id: "IE5"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 621
---

# IE5. `entity_close`

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
- **Пункт:** `IE5` — `entity_close`.
- **Критерий:** close() помечает _is_closed и возвращает close_query.
- **Статус в архивном плане:** ✅.
