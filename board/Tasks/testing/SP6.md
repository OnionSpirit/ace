---
id: "testing:SP6"
source_id: "SP6"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 806
---

# SP6. `roaming_true`

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
- **Пункт:** `SP6` — `roaming_true`.
- **Критерий:** roaming(true) → _roaming = true.
- **Статус в архивном плане:** ✅.
