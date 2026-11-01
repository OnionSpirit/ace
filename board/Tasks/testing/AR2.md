---
id: "testing:AR2"
source_id: "AR2"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 946
---

# AR2. `big_alloc_goes_to_malloc`

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
- **Пункт:** `AR2` — `big_alloc_goes_to_malloc`.
- **Критерий:** большая аллокация использует transient и сразу освобождается.
- **Статус в архивном плане:** ✅.
