---
id: testing:RN6
source_id: RN6
kind: coverage
status: Открыто
source_status: ⬜
priority: Не указан
source_document: agents/TESTING.md
source_line: 429
rank: 116736
---

# RN6. `attach_increments_tasks`

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
- **Пункт:** `RN6` — `attach_increments_tasks`.
- **Критерий:** attach() → ++_tasks_amount.
- **Статус в архивном плане:** ⬜.
