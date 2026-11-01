---
id: "testing:CB2"
source_id: "CB2"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 373
---

# CB2. `unwatch_last`

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
- **Пункт:** `CB2` — `unwatch_last`.
- **Критерий:** Последний `untrack()` обнуляет refcount.
- **Статус в архивном плане:** ✅.
