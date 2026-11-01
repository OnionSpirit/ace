---
id: "testing:CB4"
source_id: "CB4"
kind: "coverage"
status: "Решено"
source_status: "✅"
priority: "Не указан"
source_document: "agents/TESTING.md"
source_line: 375
---

# CB4. `is_untracked`

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
- **Пункт:** `CB4` — `is_untracked`.
- **Критерий:** Только нулевой refcount считается untracked.
- **Статус в архивном плане:** ✅.
