---
id: testing:RN1
source_id: RN1
kind: coverage
status: Открыто
source_status: ⬜
priority: Не указан
source_document: agents/TESTING.md
source_line: 424
rank: 105472
---

# RN1. `reattach_same_runner`

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
- **Пункт:** `RN1` — `reattach_same_runner`.
- **Критерий:** reattach на том же раннере → push_node в _pool.
- **Статус в архивном плане:** ⬜.
