---
id: testing:RN8
source_id: RN8
kind: coverage
status: Открыто
source_status: ⬜
priority: Не указан
source_document: agents/TESTING.md
source_line: 431
rank: 118784
---

# RN8. `yank_non_resumable`

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
- **Пункт:** `RN8` — `yank_non_resumable`.
- **Критерий:** yank() задачи с e_detached → release_node, --_tasks_amount.
- **Статус в архивном плане:** ⬜.
