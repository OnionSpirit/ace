---
id: "testing:SP7"
source_id: "SP7"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 807
---

# SP7. `roaming_false`

```dispatch
label: Выполнить следующую фазу
intent: take-task
tool: codex
repo: ace
prompt: "{{vault}}/{{file}}"
```

## Архивная запись

- **Раздел:** 3.17 Управление задачами.
- **Fixture:** `spawn_extra_fixture` (расширение).
- **Пункт:** `SP7` — `roaming_false`.
- **Критерий:** roaming(false) → _roaming = false.
- **Статус в архивном плане:** ✅.
