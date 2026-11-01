---
id: "testing:AR3"
source_id: "AR3"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 947
---

# AR3. `chunk_reuse_after_free`

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
- **Пункт:** `AR3` — `chunk_reuse_after_free`.
- **Критерий:** повторная аллокация переиспользует pooled chunk.
- **Статус в архивном плане:** ✅.
