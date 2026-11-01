---
id: "testing:IE2"
source_id: "IE2"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 618
---

# IE2. `entity_param_construction`

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
- **Пункт:** `IE2` — `entity_param_construction`.
- **Критерий:** entity(fd, false) сохраняет параметры.
- **Статус в архивном плане:** ✅.
