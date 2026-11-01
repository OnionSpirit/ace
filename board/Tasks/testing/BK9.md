---
id: "testing:BK9"
source_id: "BK9"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 904
---

# BK9. `insure_replaced_by_backup`

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
- **Пункт:** `BK9` — `insure_replaced_by_backup`.
- **Критерий:** регистрация backup вытесняет insure → при отмене только backup.
- **Статус в архивном плане:** ✅.
