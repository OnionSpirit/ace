---
id: "testing:BK5"
source_id: "BK5"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 900
---

# BK5. `backup_task_payload_awaited`

```dispatch
label: Выполнить следующую фазу
intent: take-task
tool: codex
repo: ace
prompt: "{{vault}}/{{file}}"
```

## Архивная запись

- **Раздел:** 3.21 futures/backup.h — backup / insure / emergency (добавлен).
- **Fixture:** `backup_fixture` (добавлен).
- **Пункт:** `BK5` — `backup_task_payload_awaited`.
- **Критерий:** task-коллбек co_await-ится до завершения (с его таймером), LIFO-смесь callable/task.
- **Статус в архивном плане:** ✅.
