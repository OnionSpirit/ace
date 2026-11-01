---
id: "testing:IE4"
source_id: "IE4"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 620
---

# IE4. `entity_extract`

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
- **Пункт:** `IE4` — `entity_extract`.
- **Критерий:** extract() возвращает FD и инвалидирует сущность.
- **Статус в архивном плане:** ✅.
