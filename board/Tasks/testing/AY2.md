---
id: "testing:AY2"
source_id: "AY2"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 596
---

# AY2. `any_construct_int`

```dispatch
label: Выполнить следующую фазу
intent: take-task
tool: codex
repo: ace
prompt: "{{vault}}/{{file}}"
```

## Архивная запись

- **Раздел:** 3.10 io.h.
- **Fixture:** `io_any_fixture`.
- **Пункт:** `AY2` — `any_construct_int`.
- **Критерий:** any(int) — placement new + корректный deleter.
- **Статус в архивном плане:** ✅.
