---
id: "testing:CB7"
source_id: "CB7"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 378
---

# CB7. `handle_cancel_no_router`

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
- **Пункт:** `CB7` — `handle_cancel_no_router`.
- **Критерий:** Cancel idle handle — безопасный no-op.
- **Статус в архивном плане:** ✅.
