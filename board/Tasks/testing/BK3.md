---
id: "testing:BK3"
source_id: "BK3"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 898
---

# BK3. `backup_destroy_incomplete_fires`

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
- **Пункт:** `BK3` — `backup_destroy_incomplete_fires`.
- **Критерий:** eager promise в main (без runner): ~async() → fire через `ace::schedule` fallback.
- **Статус в архивном плане:** ✅.
