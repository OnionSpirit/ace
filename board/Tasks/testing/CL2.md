---
id: "testing:CL2"
source_id: "CL2"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 690
---

# CL2. `timeout_while_release_budget_is_exhausted`

```dispatch
label: Выполнить следующую фазу
intent: take-task
tool: codex
repo: ace
prompt: "{{vault}}/{{file}}"
```

## Архивная запись

- **Раздел:** 3.13 services/clock.h.
- **Fixture:** `ClockFixture` (не реализована), частично покрыто через `timer_fixture`.
- **Пункт:** `CL2` — `timeout_while_release_budget_is_exhausted`.
- **Критерий:** Отстающий release cursor не сокращает новый timeout.
- **Статус в архивном плане:** ✅.
