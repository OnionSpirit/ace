---
id: "testing:BK1"
source_id: "BK1"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 896
---

# BK1. `backup_cancel_fires_lifo`

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
- **Пункт:** `BK1` — `backup_cancel_fires_lifo`.
- **Критерий:** 3 backup + cancel → выполнение в обратном порядке [3,2,1].
- **Статус в архивном плане:** ✅.
