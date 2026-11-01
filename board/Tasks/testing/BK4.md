---
id: "testing:BK4"
source_id: "BK4"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 899
---

# BK4. `backup_fire_scheduled_not_inline`

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
- **Пункт:** `BK4` — `backup_fire_scheduled_not_inline`.
- **Критерий:** fire НЕ инлайновый: маркер драйвера (100) до коллбека (1) в канале.
- **Статус в архивном плане:** ✅.
