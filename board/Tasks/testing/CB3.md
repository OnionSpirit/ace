---
id: "testing:CB3"
source_id: "CB3"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 374
---

# CB3. `watch_unwatch`

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
- **Пункт:** `CB3` — `watch_unwatch`.
- **Критерий:** `track()`/`untrack()` меняют единый refcount.
- **Статус в архивном плане:** ✅.
