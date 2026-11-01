---
id: "testing:NQ5"
source_id: "NQ5"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 760
---

# NQ5. `dynamic_queue_move_transfers_ownership`

```dispatch
label: Выполнить следующую фазу
intent: take-task
tool: codex
repo: ace
prompt: "{{vault}}/{{file}}"
```

## Архивная запись

- **Раздел:** 3.15 futures/channel.h.
- **Fixture:** `nukes_concurrency_fixture`.
- **Пункт:** `NQ5` — `dynamic_queue_move_transfers_ownership`.
- **Критерий:** Move переносит queue/dummy/pool и оставляет source inert.
- **Статус в архивном плане:** ✅.
