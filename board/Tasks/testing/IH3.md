---
id: "testing:IH3"
source_id: "IH3"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 609
---

# IH3. `hanged_command_pool_exists`

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
- **Пункт:** `IH3` — `hanged_command_pool_exists`.
- **Критерий:** command_pool thread_local доступен.
- **Статус в архивном плане:** ✅.
