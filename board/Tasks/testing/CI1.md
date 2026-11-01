---
id: "testing:CI1"
source_id: "CI1"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 723
---

# CI1. `first_allocation_failure_rethrows_and_retries`

```dispatch
label: Выполнить следующую фазу
intent: take-task
tool: codex
repo: ace
prompt: "{{vault}}/{{file}}"
```

## Архивная запись

- **Раздел:** 3.14 futures/timeout.h.
- **Fixture:** `clock_initialization_fixture`.
- **Пункт:** `CI1` — `first_allocation_failure_rethrows_and_retries`.
- **Критерий:** First-use TLS clock allocation failure доставляется в co_await; следующий timeout повторяет initialization.
- **Статус в архивном плане:** ✅.
