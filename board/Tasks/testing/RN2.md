---
id: testing:RN2
source_id: RN2
kind: coverage
status: Открыто
source_status: ⬜
priority: Не указан
source_document: agents/TESTING.md
source_line: 425
rank: 112640
---

# RN2. `reattach_different_runner`

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
- **Пункт:** `RN2` — `reattach_different_runner`.
- **Критерий:** reattach на другом раннере → push_node в _insert_pool.
- **Статус в архивном плане:** ⬜.
