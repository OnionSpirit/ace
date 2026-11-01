---
id: "testing:BK6"
source_id: "BK6"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 901
---

# BK6. `insure_fires_when_cancelled_during_protected_await`

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
- **Пункт:** `BK6` — `insure_fires_when_cancelled_during_protected_await`.
- **Критерий:** отмена на защищаемой co_await → страховка сработала.
- **Статус в архивном плане:** ✅.
