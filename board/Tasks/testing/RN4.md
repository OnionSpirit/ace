---
id: testing:RN4
source_id: RN4
kind: coverage
status: Открыто
source_status: ⬜
priority: Не указан
source_document: agents/TESTING.md
source_line: 427
rank: 114688
---

# RN4. `reattach_front_different`

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
- **Пункт:** `RN4` — `reattach_front_different`.
- **Критерий:** reattach_front на другом → _insert_pool.
- **Статус в архивном плане:** ⬜.
