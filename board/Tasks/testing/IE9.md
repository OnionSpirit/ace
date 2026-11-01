---
id: "testing:IE9"
source_id: "IE9"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 625
---

# IE9. `guard_already_closed`

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
- **Пункт:** `IE9` — `guard_already_closed`.
- **Критерий:** guard с _closed=true не закрывает FD повторно.
- **Статус в архивном плане:** ✅.
