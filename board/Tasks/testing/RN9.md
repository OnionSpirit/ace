---
id: testing:RN9
source_id: RN9
kind: coverage
status: Открыто
source_status: ⬜
priority: Не указан
source_document: agents/TESTING.md
source_line: 432
rank: 119808
---

# RN9. `yank_with_router`

```dispatch
label: Выполнить следующую фазу
intent: take-task
tool: codex
repo: ace
prompt: "{{vault}}/{{file}}"
```

## Архивная запись

- **Раздел:** 3.5 core/runner.h.
- **Fixture:** `runner_fixture`.
- **Пункт:** `RN9` — `yank_with_router`.
- **Критерий:** yank() задачи с router → redirect(node).
- **Статус в архивном плане:** ⬜.
