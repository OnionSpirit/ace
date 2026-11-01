---
id: "testing:CX1"
source_id: "CX1"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 770
---

# CX1. `try_lock_free`

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
- **Пункт:** `CX1` — `try_lock_free`.
- **Критерий:** try_lock() на свободном → true.
- **Статус в архивном плане:** ✅.
