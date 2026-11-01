---
id: "testing:CX7"
source_id: "CX7"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 776
---

# CX7. `proxy_destructor_sync`

```dispatch
label: Выполнить следующую фазу
intent: take-task
tool: codex
repo: ace
prompt: "{{vault}}/{{file}}"
```

## Архивная запись

- **Раздел:** 3.16 futures/cutex.h.
- **Fixture:** `cutex_extra_fixture` (расширение).
- **Пункт:** `CX7` — `proxy_destructor_sync`.
- **Критерий:** ~proxy() вызывает sync() автоматически.
- **Статус в архивном плане:** ✅.
