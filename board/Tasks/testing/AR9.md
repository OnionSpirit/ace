---
id: "testing:AR9"
source_id: "AR9"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 953
---

# AR9. `occupied_zero_drains_every_alloc`

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
- **Пункт:** `AR9` — `occupied_zero_drains_every_alloc`.
- **Критерий:** occupied=0 не делит на ноль и дренирует канал.
- **Статус в архивном плане:** ✅.
