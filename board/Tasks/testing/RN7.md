---
id: testing:RN7
source_id: RN7
kind: coverage
status: Открыто
source_status: ⬜
priority: Не указан
source_document: agents/TESTING.md
source_line: 430
rank: 117760
---

# RN7. `attach_front_increments_tasks`

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
- **Пункт:** `RN7` — `attach_front_increments_tasks`.
- **Критерий:** attach_front() → ++_tasks_amount.
- **Статус в архивном плане:** ⬜.
