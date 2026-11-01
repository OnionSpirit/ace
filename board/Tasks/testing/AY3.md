---
id: "testing:AY3"
source_id: "AY3"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 597
---

# AY3. `any_construct_string`

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
- **Пункт:** `AY3` — `any_construct_string`.
- **Критерий:** any(string) — placement new для non-trivial типа.
- **Статус в архивном плане:** ✅.
