---
id: "testing:CX5"
source_id: "CX5"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 774
---

# CX5. `proxy_double_capture`

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
- **Пункт:** `CX5` — `proxy_double_capture`.
- **Критерий:** Повторный capture() без sync() → logic_error.
- **Статус в архивном плане:** ✅.
