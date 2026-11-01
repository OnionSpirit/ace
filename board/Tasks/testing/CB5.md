---
id: "testing:CB5"
source_id: "CB5"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 376
---

# CB5. `get_block_from_address`

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
- **Пункт:** `CB5` — `get_block_from_address`.
- **Критерий:** Frame address преобразуется в preceding control block.
- **Статус в архивном плане:** ✅.
