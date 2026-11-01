---
id: "testing:IH4"
source_id: "IH4"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 610
---

# IH4. `hanged_command_pool_capture`

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
- **Пункт:** `IH4` — `hanged_command_pool_capture`.
- **Критерий:** capture() из пула возвращает команду.
- **Статус в архивном плане:** ✅.
