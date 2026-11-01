---
id: "testing:CX6"
source_id: "CX6"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 775
---

# CX6. `proxy_double_sync`

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
- **Пункт:** `CX6` — `proxy_double_sync`.
- **Критерий:** Повторный sync() — no-op.
- **Статус в архивном плане:** ✅.
