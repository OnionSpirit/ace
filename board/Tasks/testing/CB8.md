---
id: "testing:CB8"
source_id: "CB8"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 379
---

# CB8. `handle_copy`

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
- **Пункт:** `CB8` — `handle_copy`.
- **Критерий:** Копирование handle добавляет reference.
- **Статус в архивном плане:** ✅.
