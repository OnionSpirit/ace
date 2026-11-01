---
id: "testing:SP9"
source_id: "SP9"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 809
---

# SP9. `polling_false`

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
- **Пункт:** `SP9` — `polling_false`.
- **Критерий:** polling(false) оставляет suspended task в local pool.
- **Статус в архивном плане:** ✅.
