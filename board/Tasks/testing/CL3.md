---
id: "testing:CL3"
source_id: "CL3"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 691
---

# CL3. `timeout_positive_submillisecond_never_completes_early`

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
- **Пункт:** `CL3` — `timeout_positive_submillisecond_never_completes_early`.
- **Критерий:** Positive sub-ms округляется вверх.
- **Статус в архивном плане:** ✅.
