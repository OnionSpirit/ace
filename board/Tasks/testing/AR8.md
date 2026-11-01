---
id: "testing:AR8"
source_id: "AR8"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 952
---

# AR8. `limit_zero_drains_every_alloc`

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
- **Пункт:** `AR8` — `limit_zero_drains_every_alloc`.
- **Критерий:** max=0 дренирует канал перед каждой аллокацией.
- **Статус в архивном плане:** ✅.
