---
id: "testing:IH1"
source_id: "IH1"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 607
---

# IH1. `hanged_basic_fail_handler`

```dispatch
label: Выполнить следующую фазу
intent: take-task
tool: codex
repo: ace
prompt: "{{vault}}/{{file}}"
```

## Архивная запись

- **Раздел:** 3.10 io.h.
- **Fixture:** `io_hanged_fixture`.
- **Пункт:** `IH1` — `hanged_basic_fail_handler`.
- **Критерий:** basic_fail_handler с отрицательным res бросает runtime_error.
- **Статус в архивном плане:** ✅.
