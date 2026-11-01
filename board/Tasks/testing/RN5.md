---
id: testing:RN5
source_id: RN5
kind: coverage
status: Открыто
source_status: ⬜
priority: Не указан
source_document: agents/TESTING.md
source_line: 428
rank: 115712
---

# RN5. `reattach_idle_context`

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
- **Пункт:** `RN5` — `reattach_idle_context`.
- **Критерий:** reattach на idle контексте → runtime_error.
- **Статус в архивном плане:** ⬜.
