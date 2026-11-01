---
id: "testing:BK2"
source_id: "BK2"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 897
---

# BK2. `backup_normal_completion_no_fire`

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
- **Пункт:** `BK2` — `backup_normal_completion_no_fire`.
- **Критерий:** `co_return` → коллбеки не выполнены.
- **Статус в архивном плане:** ✅.
