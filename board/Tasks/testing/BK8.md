---
id: "testing:BK8"
source_id: "BK8"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 903
---

# BK8. `insure_dropped_when_next_op_ready`

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
- **Пункт:** `BK8` — `insure_dropped_when_next_op_ready`.
- **Критерий:** следующая операция готова синхронно (roaming) → страховка снята (механизм `_insured_prev`).
- **Статус в архивном плане:** ✅.
