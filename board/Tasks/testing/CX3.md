---
id: testing:CX3
source_id: CX3
kind: coverage
status: Открыто
source_status: ⬜
priority: Не указан
source_document: agents/TESTING.md
source_line: 772
rank: 84992
---

# CX3. `cutex_capture_suspends`

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
- **Пункт:** `CX3` — `cutex_capture_suspends`.
- **Критерий:** capture() на занятом → корутина суспендится.
- **Статус в архивном плане:** ⬜.
