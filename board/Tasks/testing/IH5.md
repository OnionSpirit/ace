---
id: "testing:IH5"
source_id: "IH5"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 611
---

# IH5. `hanged_command_defaults`

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
- **Пункт:** `IH5` — `hanged_command_defaults`.
- **Критерий:** Captured command безопасно возвращается в pool; очистка payload этим тестом не проверяется.
- **Статус в архивном плане:** ✅.
