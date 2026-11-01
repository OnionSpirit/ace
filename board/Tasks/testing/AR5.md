---
id: "testing:AR5"
source_id: "AR5"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 949
---

# AR5. `arena_is_thread_local`

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
- **Пункт:** `AR5` — `arena_is_thread_local`.
- **Критерий:** у каждого треда свой singleton arena.
- **Статус в архивном плане:** ✅.
