---
id: "testing:IE3"
source_id: "IE3"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 619
---

# IE3. `entity_move`

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
- **Пункт:** `IE3` — `entity_move`.
- **Критерий:** Move-конструктор переносит FD, источник инвалидируется.
- **Статус в архивном плане:** ✅.
