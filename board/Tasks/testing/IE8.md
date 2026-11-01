---
id: "testing:IE8"
source_id: "IE8"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 624
---

# IE8. `guard_valid_fd_no_runner`

```dispatch
label: Выполнить следующую фазу
intent: take-task
tool: codex
repo: ace
prompt: "{{vault}}/{{file}}"
```

## Архивная запись

- **Раздел:** 3.10 io.h.
- **Fixture:** `io_entity_fixture`.
- **Пункт:** `IE8` — `guard_valid_fd_no_runner`.
- **Критерий:** guard с валидным FD без раннера: schedule pending_close.
- **Статус в архивном плане:** ✅.
