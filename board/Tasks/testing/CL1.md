---
id: "testing:CL1"
source_id: "CL1"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 689
---

# CL1. `timeout_uses_registration_timestamp`

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
- **Пункт:** `CL1` — `timeout_uses_registration_timestamp`.
- **Критерий:** Блокировка до subscribe не сокращает новый timeout.
- **Статус в архивном плане:** ✅.
