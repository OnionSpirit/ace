---
id: "testing:CB9"
source_id: "CB9"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 380
---

# CB9. `handle_done`

```dispatch
label: Выполнить следующую фазу
intent: take-task
tool: codex
repo: ace
prompt: "{{vault}}/{{file}}"
```

## Архивная запись

- **Раздел:** 3.3 core/control.h.
- **Fixture:** `control_block_fixture`.
- **Пункт:** `CB9` — `handle_done`.
- **Критерий:** Failed lifecycle считается terminal.
- **Статус в архивном плане:** ✅.
