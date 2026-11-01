---
id: "testing:SP3"
source_id: "SP3"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 803
---

# SP3. `post_uses_attach_front`

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
- **Пункт:** `SP3` — `post_uses_attach_front`.
- **Критерий:** post → задача в начало очереди.
- **Статус в архивном плане:** ✅.
