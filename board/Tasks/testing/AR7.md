---
id: "testing:AR7"
source_id: "AR7"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 951
---

# AR7. `channel_drain_cadence`

```dispatch
label: Выполнить следующую фазу
intent: take-task
tool: codex
repo: ace
prompt: "{{vault}}/{{file}}"
```

## Архивная запись

- **Раздел:** 3.22 core/arena.h — arena_fixture.
- **Fixture:** `backup_fixture` (добавлен).
- **Пункт:** `AR7` — `channel_drain_cadence`.
- **Критерий:** формула `N=max/occupied` управляет drain cadence.
- **Статус в архивном плане:** ✅.
