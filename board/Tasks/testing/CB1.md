---
id: "testing:CB1"
source_id: "CB1"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 372
---

# CB1. `control_block_init`

```dispatch
label: Выполнить следующую фазу
intent: take-task
tool: codex
repo: ace
prompt: "{{vault}}/{{file}}"
```

## Архивная запись

- **Раздел:** 3.3 core/control.h.
- **Fixture:** `control_block_fixture`.
- **Пункт:** `CB1` — `control_block_init`.
- **Критерий:** Default refcount, frame size и lifecycle status.
- **Статус в архивном плане:** ✅.
