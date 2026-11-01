---
id: "testing:AR4"
source_id: "AR4"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 948
---

# AR4. `pool_never_returns_to_system`

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
- **Пункт:** `AR4` — `pool_never_returns_to_system`.
- **Критерий:** освобождённые pooled chunks удерживаются до деструктора arena.
- **Статус в архивном плане:** ✅.
