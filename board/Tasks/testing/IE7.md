---
id: "testing:IE7"
source_id: "IE7"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 623
---

# IE7. `entity_guard_no_runner`

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
- **Пункт:** `IE7` — `entity_guard_no_runner`.
- **Критерий:** guard с невалидным FD не падает.
- **Статус в архивном плане:** ✅.
