---
id: "testing:AR6"
source_id: "AR6"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 950
---

# AR6. `cross_thread_free_returns_to_owner`

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
- **Пункт:** `AR6` — `cross_thread_free_returns_to_owner`.
- **Критерий:** foreign pooled-free возвращается владельцу через intrusive stack.
- **Статус в архивном плане:** ✅.
