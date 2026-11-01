---
id: "testing:CL4"
source_id: "CL4"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 692
---

# CL4. `expire_past_deadline_beats_new_relative_timeout`

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
- **Пункт:** `CL4` — `expire_past_deadline_beats_new_relative_timeout`.
- **Критерий:** Absolute deadline сохраняется до routing.
- **Статус в архивном плане:** ✅.
