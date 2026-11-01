---
id: testing:RN3
source_id: RN3
kind: coverage
status: Открыто
source_status: ⬜
priority: Не указан
source_document: agents/TESTING.md
source_line: 426
rank: 113664
---

# RN3. `reattach_front_same`

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
- **Пункт:** `RN3` — `reattach_front_same`.
- **Критерий:** reattach_front → push_node_front.
- **Статус в архивном плане:** ⬜.
