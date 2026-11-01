---
id: "testing:CB6"
source_id: "CB6"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 377
---

# CB6. `control_block_handle_default`

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
- **Пункт:** `CB6` — `control_block_handle_default`.
- **Критерий:** Default handle idle и не terminal.
- **Статус в архивном плане:** ✅.
