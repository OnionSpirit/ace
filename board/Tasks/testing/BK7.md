---
id: "testing:BK7"
source_id: "BK7"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 902
---

# BK7. `insure_dropped_after_passing_protected_await`

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
- **Пункт:** `BK7` — `insure_dropped_after_passing_protected_await`.
- **Критерий:** защищаемая co_await пройдена → страховка снята.
- **Статус в архивном плане:** ✅.
